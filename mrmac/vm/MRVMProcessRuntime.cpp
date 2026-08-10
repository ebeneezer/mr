#include "MRVMProcessRuntime.hpp"

#include "MRVMRuntimeState.hpp"
#include "MRVMValue.hpp"

#include "../../app/utils/MRStringUtils.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <limits>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

std::vector<MRVMForkedProcess> runtimeForkedProcesses() {
	const std::vector<int> pids = mrvmRuntimeStateIntList("forkedProcesses", "pids");
	const std::vector<std::string> sourcePaths = mrvmRuntimeStateStringList("forkedProcesses", "sourcePaths");
	const std::vector<std::string> pdfPaths = mrvmRuntimeStateStringList("forkedProcesses", "pdfPaths");
	std::vector<MRVMForkedProcess> processes;

	processes.reserve(pids.size());
	for (std::size_t i = 0; i < pids.size(); ++i) {
		MRVMForkedProcess process;
		process.pid = pids[i];
		if (i < sourcePaths.size()) process.sourcePath = sourcePaths[i];
		if (i < pdfPaths.size()) process.pdfPath = pdfPaths[i];
		process.ownerBufferIds = mrvmRuntimeStateIntList("forkedProcessOwners", std::to_string(i));
		processes.push_back(process);
	}
	return processes;
}

void storeRuntimeForkedProcesses(const std::vector<MRVMForkedProcess> &processes) {
	std::vector<int> pids;
	std::vector<std::string> sourcePaths;
	std::vector<std::string> pdfPaths;

	pids.reserve(processes.size());
	sourcePaths.reserve(processes.size());
	pdfPaths.reserve(processes.size());
	mrvmClearRuntimeStateBranch("forkedProcessOwners");
	for (std::size_t i = 0; i < processes.size(); ++i) {
		const MRVMForkedProcess &process = processes[i];
		pids.push_back(process.pid);
		sourcePaths.push_back(process.sourcePath);
		pdfPaths.push_back(process.pdfPath);
		mrvmStoreRuntimeStateIntList("forkedProcessOwners", std::to_string(i), process.ownerBufferIds);
	}
	mrvmStoreRuntimeStateIntList("forkedProcesses", "pids", pids);
	mrvmStoreRuntimeStateStringList("forkedProcesses", "sourcePaths", sourcePaths);
	mrvmStoreRuntimeStateStringList("forkedProcesses", "pdfPaths", pdfPaths);
}

} // namespace

void mrvmProcessRuntimeSetContext(int argc, char **argv) {
	std::string startupCommand;
	std::vector<std::string> processArgs;
	std::string executablePath;
	std::string shellPath;

	if (argc > 0 && argv != nullptr && argv[0] != nullptr) startupCommand = argv[0];
	for (int i = 1; argv != nullptr && i < argc; ++i)
		processArgs.push_back(argv[i] != nullptr ? std::string(argv[i]) : std::string());
	executablePath = mrvmDetectExecutablePathFromProc();
	if (executablePath.empty() && !startupCommand.empty()) executablePath = startupCommand;
	shellPath = mrvmDetectShellPath();
	mrvmStoreRuntimeStateString("process", "startupCommand", startupCommand);
	mrvmStoreRuntimeStateStringList("process", "arguments", processArgs);
	mrvmStoreRuntimeStateString("process", "executablePath", executablePath);
	mrvmStoreRuntimeStateString("process", "executableDir", mrvmDetectExecutableDir(startupCommand));
	mrvmStoreRuntimeStateString("process", "shellPath", shellPath);
}

std::vector<std::string> mrvmProcessRuntimeArguments() {
	return mrvmRuntimeStateStringList("process", "arguments");
}

static void mrvmReapForkedProcesses() {
	const std::vector<MRVMForkedProcess> processes = runtimeForkedProcesses();
	std::vector<MRVMForkedProcess> live;

	live.reserve(processes.size());
	for (const MRVMForkedProcess &process : processes) {
		int waitStatus = 0;
		const pid_t pid = static_cast<pid_t>(process.pid);
		const pid_t waited = ::waitpid(pid, &waitStatus, WNOHANG);

		if (waited == 0 || (waited < 0 && errno == EINTR)) live.push_back(process);
	}
	storeRuntimeForkedProcesses(live);
}

static bool processHasOwner(const MRVMForkedProcess &process, int ownerBufferId) {
	return ownerBufferId > 0 && std::find(process.ownerBufferIds.begin(), process.ownerBufferIds.end(), ownerBufferId) != process.ownerBufferIds.end();
}

static void addProcessOwner(MRVMForkedProcess &process, int ownerBufferId) {
	if (ownerBufferId <= 0 || processHasOwner(process, ownerBufferId)) return;
	process.ownerBufferIds.push_back(ownerBufferId);
}

static void terminateForkedProcess(int pid) {
	int waitStatus = 0;
	bool exited = false;

	if (pid <= 0) return;
	if (::kill(-static_cast<pid_t>(pid), SIGTERM) != 0 && errno == ESRCH) ::kill(static_cast<pid_t>(pid), SIGTERM);
	for (int attempt = 0; attempt < 10; ++attempt) {
		const pid_t waited = ::waitpid(static_cast<pid_t>(pid), &waitStatus, WNOHANG);

		if (waited == static_cast<pid_t>(pid) || (waited < 0 && errno == ECHILD)) {
			exited = true;
			break;
		}
		if (waited < 0 && errno != EINTR) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	if (!exited) {
		if (::kill(-static_cast<pid_t>(pid), SIGKILL) != 0 && errno == ESRCH) ::kill(static_cast<pid_t>(pid), SIGKILL);
		while (::waitpid(static_cast<pid_t>(pid), &waitStatus, 0) < 0 && errno == EINTR)
			;
	}
}

static void removeOwnerFromProcess(MRVMForkedProcess &process, int ownerBufferId) {
	if (ownerBufferId <= 0) return;
	process.ownerBufferIds.erase(std::remove(process.ownerBufferIds.begin(), process.ownerBufferIds.end(), ownerBufferId), process.ownerBufferIds.end());
}

static void removeOwnerFromMismatchedPdfProcesses(int ownerBufferId, const std::string &pdfPath) {
	std::vector<MRVMForkedProcess> processes = runtimeForkedProcesses();
	std::vector<MRVMForkedProcess> live;

	if (ownerBufferId <= 0 || pdfPath.empty()) return;
	live.reserve(processes.size());
	for (MRVMForkedProcess &process : processes) {
		if (processHasOwner(process, ownerBufferId) && process.pdfPath != pdfPath) removeOwnerFromProcess(process, ownerBufferId);
		if (!process.ownerBufferIds.empty() || process.pdfPath.empty()) live.push_back(process);
		else
			terminateForkedProcess(process.pid);
	}
	storeRuntimeForkedProcesses(live);
}

int mrvmForkProcess(const std::vector<std::string> &arguments, int ownerBufferId, const std::string &sourcePath, const std::string &pdfPath) {
	std::vector<std::string> commandArguments = arguments;
	std::vector<char *> argv;
	int errorPipe[2] = {-1, -1};
	pid_t childPid = -1;
	int execError = 0;

	if (commandArguments.empty()) return EINVAL;
	commandArguments[0] = trimAscii(commandArguments[0]);
	if (commandArguments[0].empty()) return EINVAL;
	argv.reserve(commandArguments.size() + 1);
	for (std::string &argument : commandArguments)
		argv.push_back(argument.data());
	argv.push_back(nullptr);

	mrvmReapForkedProcesses();
	removeOwnerFromMismatchedPdfProcesses(ownerBufferId, pdfPath);
	if (!pdfPath.empty()) {
		std::vector<MRVMForkedProcess> processes = runtimeForkedProcesses();
		for (MRVMForkedProcess &process : processes) {
			if (process.pdfPath != pdfPath) continue;
			addProcessOwner(process, ownerBufferId);
			storeRuntimeForkedProcesses(processes);
			return 0;
		}
	}
	if (::pipe(errorPipe) != 0) return errno != 0 ? errno : 1;
	(void)::fcntl(errorPipe[1], F_SETFD, FD_CLOEXEC);
	childPid = ::fork();
	if (childPid < 0) {
		const int savedErrno = errno != 0 ? errno : 1;
		::close(errorPipe[0]);
		::close(errorPipe[1]);
		return savedErrno;
	}
	if (childPid == 0) {
		int nullIn = -1;
		int nullOut = -1;
		int savedErrno = 0;

		::close(errorPipe[0]);
		::setpgid(0, 0);
		nullIn = ::open("/dev/null", O_RDONLY);
		if (nullIn >= 0) {
			::dup2(nullIn, STDIN_FILENO);
			::close(nullIn);
		}
		nullOut = ::open("/dev/null", O_WRONLY);
		if (nullOut >= 0) {
			::dup2(nullOut, STDOUT_FILENO);
			::dup2(nullOut, STDERR_FILENO);
			::close(nullOut);
		}
		::execvp(argv[0], argv.data());
		savedErrno = errno != 0 ? errno : 1;
		(void)::write(errorPipe[1], &savedErrno, sizeof(savedErrno));
		::_exit(127);
	}
	::close(errorPipe[1]);
	::setpgid(childPid, childPid);
	{
		std::size_t readBytes = 0;
		char *target = reinterpret_cast<char *>(&execError);

		while (readBytes < sizeof(execError)) {
			const ssize_t count = ::read(errorPipe[0], target + readBytes, sizeof(execError) - readBytes);

			if (count == 0) break;
			if (count > 0) {
				readBytes += static_cast<std::size_t>(count);
				continue;
			}
			if (errno == EINTR) continue;
			execError = errno != 0 ? errno : 1;
			break;
		}
	}
	::close(errorPipe[0]);
	if (execError != 0) {
		int waitStatus = 0;
		while (::waitpid(childPid, &waitStatus, 0) < 0 && errno == EINTR)
			;
		return execError;
	}
	{
		MRVMForkedProcess process;

		process.pid = static_cast<int>(childPid);
		process.sourcePath = sourcePath;
		process.pdfPath = pdfPath;
		addProcessOwner(process, ownerBufferId);
		std::vector<MRVMForkedProcess> processes = runtimeForkedProcesses();
		processes.push_back(process);
		storeRuntimeForkedProcesses(processes);
	}
	return 0;
}

void mrvmCloseForksForOwner(int ownerBufferId) {
	std::vector<MRVMForkedProcess> processes;
	std::vector<MRVMForkedProcess> live;

	if (ownerBufferId <= 0) return;
	mrvmReapForkedProcesses();
	processes = runtimeForkedProcesses();
	live.reserve(processes.size());
	for (MRVMForkedProcess &process : processes) {
		removeOwnerFromProcess(process, ownerBufferId);
		if (!process.ownerBufferIds.empty() || process.pdfPath.empty()) live.push_back(process);
		else
			terminateForkedProcess(process.pid);
	}
	storeRuntimeForkedProcesses(live);
}

void mrvmCloseAllForkedProcesses() {
	mrvmReapForkedProcesses();
	const std::vector<MRVMForkedProcess> processes = runtimeForkedProcesses();
	for (const MRVMForkedProcess &process : processes)
		terminateForkedProcess(process.pid);
	storeRuntimeForkedProcesses(std::vector<MRVMForkedProcess>());
}

std::string mrvmCommandFirstLine(const std::string &command) {
	std::string line;
	char buffer[512];
	std::string shellPath = mrvmDetectShellPath();
	int pipeFds[2] = {-1, -1};
	pid_t childPid = -1;
	int waitStatus = 0;

	if (command.empty() || shellPath.empty()) return std::string();
	if (::pipe(pipeFds) != 0) return std::string();
	childPid = ::fork();
	if (childPid < 0) {
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		return std::string();
	}
	if (childPid == 0) {
		::dup2(pipeFds[1], STDOUT_FILENO);
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		::execl(shellPath.c_str(), shellPath.c_str(), "-lc", command.c_str(), static_cast<char *>(nullptr));
		::_exit(127);
	}
	::close(pipeFds[1]);
	for (;;) {
		const ssize_t count = ::read(pipeFds[0], buffer, sizeof(buffer));

		if (count > 0) {
			for (std::size_t index = 0; index < static_cast<std::size_t>(count); ++index) {
				if (buffer[index] == '\n') {
					::close(pipeFds[0]);
					while (::waitpid(childPid, &waitStatus, 0) < 0 && errno == EINTR)
						;
					while (!line.empty() && line.back() == '\r')
						line.pop_back();
					return line;
				}
				line.push_back(buffer[index]);
			}
			continue;
		}
		if (count == 0) break;
		if (errno == EINTR) continue;
		break;
	}
	::close(pipeFds[0]);
	while (::waitpid(childPid, &waitStatus, 0) < 0 && errno == EINTR)
		;
	while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
		line.pop_back();
	return line;
}

int mrvmRunShellCommand(const std::string &command, const std::string &shellPath) {
	pid_t childPid = -1;
	int waitStatus = -1;

	if (command.empty() || shellPath.empty()) return -1;
	childPid = ::fork();
	if (childPid < 0) return -1;
	if (childPid == 0) {
		::execl(shellPath.c_str(), shellPath.c_str(), "-lc", command.c_str(), static_cast<char *>(nullptr));
		::_exit(127);
	}
	while (::waitpid(childPid, &waitStatus, 0) < 0) {
		if (errno == EINTR) continue;
		return -1;
	}
	return waitStatus;
}

std::string mrvmDetectExecutablePathFromProc() {
	char buffer[PATH_MAX + 1];
	ssize_t len = ::readlink("/proc/self/exe", buffer, PATH_MAX);
	if (len <= 0) return std::string();
	buffer[len] = '\0';
	return std::string(buffer);
}

std::string mrvmNormalizeDirPath(const std::string &path) {
	if (path.empty()) return std::string("./");
	std::string out = path;
	if (out.back() != '/') out.push_back('/');
	return out;
}

std::string mrvmDetectExecutableDir(const std::string &argv0) {
	std::string path = mrvmDetectExecutablePathFromProc();
	if (path.empty()) path = argv0;
	if (path.empty()) {
		char cwd[PATH_MAX + 1];
		if (::getcwd(cwd, sizeof(cwd)) != nullptr) return mrvmNormalizeDirPath(std::string(cwd));
		return std::string("./");
	}
	std::size_t sep = path.find_last_of('/');
	if (sep == std::string::npos) {
		char cwd[PATH_MAX + 1];
		if (::getcwd(cwd, sizeof(cwd)) != nullptr) return mrvmNormalizeDirPath(std::string(cwd));
		return std::string("./");
	}
	return mrvmNormalizeDirPath(path.substr(0, sep));
}

std::string mrvmDetectShellPath() {
	const char *comspec = std::getenv("COMSPEC");
	if (comspec != nullptr && *comspec != '\0') return std::string(comspec);
	const char *shell = std::getenv("SHELL");
	if (shell != nullptr && *shell != '\0') return std::string(shell);
	return std::string("/bin/sh");
}

std::string mrvmDetectShellVersion(const std::string &shellPath) {
	if (shellPath.empty()) return std::string();
	std::string base = mrvmTruncatePathPart(shellPath);
	std::string command = "'";
	for (char i : shellPath) {
		if (i == '\'') command += "'\\''";
		else
			command.push_back(i);
	}
	command += "' --version 2>/dev/null";
	std::string line = mrvmCommandFirstLine(command);
	if (!line.empty()) return line;
	return base;
}

int mrvmDetectCpuCode() {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
	return 3;
#elif defined(__aarch64__) || defined(__arm__) || defined(__riscv) || defined(__powerpc__) || defined(__ppc64__)
	return 3;
#else
	return 3;
#endif
}

std::string mrvmGetenvValue(const std::string &name) {
	const char *value = std::getenv(name.c_str());
	if (value == nullptr) return std::string();
	return std::string(value);
}

bool mrvmChangeDirectoryPath(const std::string &path) {
	std::string expanded = mrvmProcessExpandUserPath(trimAscii(path));
	if (expanded.empty()) return false;
	return ::chdir(expanded.c_str()) == 0;
}

bool mrvmDeleteFilePath(const std::string &path) {
	std::string expanded = mrvmProcessExpandUserPath(trimAscii(path));
	if (expanded.empty()) return false;
	return std::remove(expanded.c_str()) == 0;
}

std::string mrvmProcessExpandUserPath(const std::string &path) {
	if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0') return std::string(home) + path.substr(1);
	}
	return path;
}

bool mrvmFileExistsPath(const std::string &path) {
	struct stat st;
	std::string expanded = mrvmProcessExpandUserPath(trimAscii(path));
	if (expanded.empty()) return false;
	return ::stat(expanded.c_str(), &st) == 0;
}

static int inferDosFileAttributes(const std::string &path, const struct stat &st) {
	int attr = 0;
	std::string name = mrvmTruncatePathPart(path);

	if (!name.empty() && name.front() == '.') attr |= 0x02;
	if (S_ISDIR(st.st_mode)) attr |= 0x10;
	else
		attr |= 0x20;
	if (::access(path.c_str(), W_OK) != 0) attr |= 0x01;
	return attr;
}

bool mrvmReadFileMetadata(const std::string &path, int *attrOut, int *sizeOut, int *timeOut) {
	struct stat st;
	std::string expanded = mrvmProcessExpandUserPath(trimAscii(path));

	if (expanded.empty() || ::stat(expanded.c_str(), &st) != 0) return false;

	if (attrOut != nullptr) *attrOut = inferDosFileAttributes(expanded, st);
	if (sizeOut != nullptr) {
		long long size = static_cast<long long>(st.st_size);
		if (size < 0) size = 0;
		if (size > std::numeric_limits<int>::max()) size = std::numeric_limits<int>::max();
		*sizeOut = static_cast<int>(size);
	}
	if (timeOut != nullptr) {
		std::tm localTime{};
		if (::localtime_r(&st.st_mtime, &localTime) == nullptr) *timeOut = 0;
		else {
			const int dosDate = ((std::max(0, localTime.tm_year + 1900 - 1980) & 0x7F) << 9) | (((localTime.tm_mon + 1) & 0x0F) << 5) | (localTime.tm_mday & 0x1F);
			const int dosTime = ((localTime.tm_hour & 0x1F) << 11) | ((localTime.tm_min & 0x3F) << 5) | ((localTime.tm_sec / 2) & 0x1F);
			*timeOut = (dosDate << 16) | dosTime;
		}
	}
	return true;
}

MRVMSubshellResult mrvmRunSubshellCapture(const std::string &command, int timeoutMs, const std::string &shellPath) {
	MRVMSubshellResult result;
	int pipeFds[2] = {-1, -1};
	pid_t childPid = -1;
	int waitStatus = 0;
	bool pipeOpen = true;
	bool childExited = false;
	bool timedOut = false;
	int timeoutPolls = 0;
	std::array<char, 4096> buffer{};
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

	result.errorLevel = 0;
	if (timeoutMs <= 0 || shellPath.empty()) {
		result.errorLevel = EINVAL;
		return result;
	}
	if (::pipe(pipeFds) != 0) {
		result.errorLevel = errno != 0 ? errno : 1;
		return result;
	}
	childPid = ::fork();
	if (childPid < 0) {
		result.errorLevel = errno != 0 ? errno : 1;
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		return result;
	}
	if (childPid == 0) {
		::setpgid(0, 0);
		::dup2(pipeFds[1], STDOUT_FILENO);
		::dup2(pipeFds[1], STDERR_FILENO);
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		::execl(shellPath.c_str(), shellPath.c_str(), "-lc", command.c_str(), static_cast<char *>(nullptr));
		::_exit(127);
	}
	::close(pipeFds[1]);
	::setpgid(childPid, childPid);
	{
		const int flags = ::fcntl(pipeFds[0], F_GETFL, 0);
		if (flags >= 0) ::fcntl(pipeFds[0], F_SETFL, flags | O_NONBLOCK);
	}
	while (pipeOpen || !childExited) {
		struct pollfd pfd;
		int pollResult = 0;

		if (!childExited && !timedOut && std::chrono::steady_clock::now() >= deadline) {
			timedOut = true;
			::kill(-childPid, SIGTERM);
		}
		if (!childExited && timedOut) {
			if (timeoutPolls > 10) ::kill(-childPid, SIGKILL);
			++timeoutPolls;
		}
		pfd.fd = pipeFds[0];
		pfd.events = POLLIN | POLLHUP;
		pfd.revents = 0;
		pollResult = pipeOpen ? ::poll(&pfd, 1, 50) : 0;
		if (pollResult < 0 && errno != EINTR) {
			result.errorLevel = errno != 0 ? errno : 1;
			break;
		}
		if (pipeOpen && pollResult > 0) {
			for (;;) {
				const ssize_t count = ::read(pipeFds[0], buffer.data(), buffer.size());
				if (count > 0) {
					result.output.append(buffer.data(), static_cast<std::size_t>(count));
					continue;
				}
				if (count == 0) {
					pipeOpen = false;
					break;
				}
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
				result.errorLevel = errno != 0 ? errno : 1;
				pipeOpen = false;
				break;
			}
		}
		if (!pipeOpen && !childExited) std::this_thread::sleep_for(std::chrono::milliseconds(50));
		if (!childExited) {
			const pid_t waited = ::waitpid(childPid, &waitStatus, WNOHANG);
			if (waited == childPid) childExited = true;
			else if (waited < 0 && errno != EINTR) {
				result.errorLevel = errno != 0 ? errno : 1;
				break;
			}
		}
		if (childExited && pipeOpen && pollResult == 0) pipeOpen = false;
	}
	if (pipeFds[0] >= 0) ::close(pipeFds[0]);
	if (!childExited && childPid > 0) {
		::kill(-childPid, SIGKILL);
		while (::waitpid(childPid, &waitStatus, 0) < 0 && errno == EINTR)
			;
		childExited = true;
	}
	if (result.errorLevel != 0) return result;
	if (timedOut) {
		result.errorLevel = 124;
		return result;
	}
	if (WIFEXITED(waitStatus)) result.errorLevel = WEXITSTATUS(waitStatus);
	else if (WIFSIGNALED(waitStatus))
		result.errorLevel = 128 + WTERMSIG(waitStatus);
	else
		result.errorLevel = 1;
	return result;
}
