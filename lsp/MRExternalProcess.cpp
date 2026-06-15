#include "MRExternalProcess.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace mr::lsp {
namespace {
bool setError(std::string &errorMessage, const std::string &message) {
	errorMessage = message;
	return false;
}

bool setFdNonBlocking(int fd, std::string &errorMessage) {
	const int flags = ::fcntl(fd, F_GETFL, 0);

	if (flags < 0) return setError(errorMessage, std::string("fcntl(F_GETFL) failed: ") + std::strerror(errno));
	if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return setError(errorMessage, std::string("fcntl(F_SETFL) failed: ") + std::strerror(errno));
	return true;
}

void closeFd(int &fd) {
	if (fd >= 0) {
		::close(fd);
		fd = -1;
	}
}

bool executableExists(const std::string &path) {
	return !path.empty() && ::access(path.c_str(), X_OK) == 0;
}

void redirectStderrToNull() {
	int nullFd = ::open("/dev/null", O_WRONLY);

	if (nullFd < 0) return;
	::dup2(nullFd, STDERR_FILENO);
	::close(nullFd);
}

int waitStatusToExitStatus(int waitStatus) {
	if (WIFEXITED(waitStatus)) return WEXITSTATUS(waitStatus);
	if (WIFSIGNALED(waitStatus)) return -WTERMSIG(waitStatus);
	return -1;
}
} // namespace

ExternalProcessSession::~ExternalProcessSession() {
	close();
}

bool ExternalProcessSession::start(const ExternalProcessSpec &spec, std::string &errorMessage) {
	int inputPipe[2] = {-1, -1};
	int outputPipe[2] = {-1, -1};

	if (running()) return setError(errorMessage, "External process session is already running.");
	if (!executableExists(spec.executablePath)) return setError(errorMessage, "External process executable is missing or not executable: " + spec.executablePath);
	if (::pipe(inputPipe) != 0) return setError(errorMessage, std::string("stdin pipe failed: ") + std::strerror(errno));
	if (::pipe(outputPipe) != 0) {
		closeFd(inputPipe[0]);
		closeFd(inputPipe[1]);
		return setError(errorMessage, std::string("output pipe failed: ") + std::strerror(errno));
	}

	childPid = ::fork();
	if (childPid < 0) {
		closeFd(inputPipe[0]);
		closeFd(inputPipe[1]);
		closeFd(outputPipe[0]);
		closeFd(outputPipe[1]);
		return setError(errorMessage, std::string("fork failed: ") + std::strerror(errno));
	}

	if (childPid == 0) {
		std::vector<char *> argv;

		::setpgid(0, 0);
		::dup2(inputPipe[0], STDIN_FILENO);
		::dup2(outputPipe[1], STDOUT_FILENO);
		redirectStderrToNull();
		closeFd(inputPipe[0]);
		closeFd(inputPipe[1]);
		closeFd(outputPipe[0]);
		closeFd(outputPipe[1]);
		if (!spec.workingDirectory.empty() && ::chdir(spec.workingDirectory.c_str()) != 0) ::_exit(126);
		argv.push_back(const_cast<char *>(spec.executablePath.c_str()));
		for (const std::string &argument : spec.arguments)
			argv.push_back(const_cast<char *>(argument.c_str()));
		argv.push_back(nullptr);
		::execv(spec.executablePath.c_str(), argv.data());
		::_exit(127);
	}

	::setpgid(childPid, childPid);
	closeFd(inputPipe[0]);
	closeFd(outputPipe[1]);
	stdinFd = inputPipe[1];
	outputFd = outputPipe[0];
	childRunning = true;
	if (!setFdNonBlocking(stdinFd, errorMessage) || !setFdNonBlocking(outputFd, errorMessage)) {
		close();
		return false;
	}
	errorMessage.clear();
	return true;
}

bool ExternalProcessSession::writeStdin(const std::string &text, std::string &errorMessage) {
	std::size_t written = 0;

	if (!running() || stdinFd < 0) return setError(errorMessage, "External process stdin is not open.");
	while (written < text.size()) {
		struct pollfd pfd;
		pfd.fd = stdinFd;
		pfd.events = POLLOUT;
		pfd.revents = 0;
		const int pollResult = ::poll(&pfd, 1, 100);
		if (pollResult < 0 && errno != EINTR) return setError(errorMessage, std::string("stdin poll failed: ") + std::strerror(errno));
		if (pollResult <= 0) continue;
		const ssize_t count = ::write(stdinFd, text.data() + written, text.size() - written);
		if (count > 0) {
			written += static_cast<std::size_t>(count);
			continue;
		}
		if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
		return setError(errorMessage, std::string("stdin write failed: ") + std::strerror(errno));
	}
	errorMessage.clear();
	return true;
}

bool ExternalProcessSession::readAvailable(std::string &out, std::string &errorMessage) {
	char buffer[4096];

	out.clear();
	if (outputFd < 0) {
		errorMessage.clear();
		return true;
	}
	for (;;) {
		const ssize_t count = ::read(outputFd, buffer, sizeof(buffer));
		if (count > 0) {
			out.append(buffer, static_cast<std::size_t>(count));
			continue;
		}
		if (count == 0) {
			closeOutputPipe();
			errorMessage.clear();
			return true;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
			errorMessage.clear();
			return true;
		}
		return setError(errorMessage, std::string("stdout read failed: ") + std::strerror(errno));
	}
}

void ExternalProcessSession::requestStop() {
	if (childRunning && childPid > 0) ::kill(-childPid, SIGTERM);
}

bool ExternalProcessSession::wait(int timeoutMs, int &exitStatus) {
	int elapsedMs = 0;
	int waitStatus = 0;

	exitStatus = -1;
	if (!childRunning || childPid <= 0) return true;
	while (timeoutMs < 0 || elapsedMs <= timeoutMs) {
		const pid_t waited = ::waitpid(childPid, &waitStatus, WNOHANG);
		if (waited == childPid) {
			exitStatus = waitStatusToExitStatus(waitStatus);
			childRunning = false;
			childPid = -1;
			closeParentPipes();
			return true;
		}
		if (waited < 0 && errno != EINTR) {
			childRunning = false;
			childPid = -1;
			closeParentPipes();
			return false;
		}
		::poll(nullptr, 0, 10);
		elapsedMs += 10;
	}
	return false;
}

bool ExternalProcessSession::running() const noexcept {
	return childRunning;
}

void ExternalProcessSession::close() {
	int exitStatus = -1;

	if (childRunning && childPid > 0) {
		requestStop();
		if (!wait(500, exitStatus)) {
			::kill(-childPid, SIGKILL);
			wait(-1, exitStatus);
		}
	}
	closeParentPipes();
}

void ExternalProcessSession::closeParentPipes() {
	closeInputPipe();
	closeOutputPipe();
}

void ExternalProcessSession::closeInputPipe() {
	closeFd(stdinFd);
}

void ExternalProcessSession::closeOutputPipe() {
	closeFd(outputFd);
}

} // namespace mr::lsp
