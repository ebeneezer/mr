#include "MRExternalCommand.hpp"
#include "../utils/MRStringUtils.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../mrmac/MRMacroRunner.hpp"
#include "../../mrmac/mrmac.h"
#include "../../mrmac/vm/MRVMRuntimeGlobals.hpp"
#include "../../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../../mrmac/vm/MRVMValue.hpp"

#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace {
[[nodiscard]] std::string trimPathInput(std::string_view path) {
	std::size_t start = 0;
	std::size_t end = path.size();

	while (start < end && std::isspace(static_cast<unsigned char>(path[start])) != 0)
		++start;
	while (end > start && (std::isspace(static_cast<unsigned char>(path[end - 1])) != 0 || static_cast<unsigned char>(path[end - 1]) < 32))
		--end;

	std::string result(path.substr(start, end - start));
	if (result.size() >= 2 && ((result.front() == '"' && result.back() == '"') || (result.front() == '\'' && result.back() == '\''))) result = result.substr(1, result.size() - 2);
	return result;
}

std::string shellQuote(const std::string &value) {
	std::string out = "'";

	for (char ch : value) {
		if (ch == '\'') out += "'\\''";
		else
			out.push_back(ch);
	}
	out.push_back('\'');
	return out;
}

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

std::string compilerOutputPathForSource(const std::string &sourcePath) {
	std::filesystem::path source(sourcePath);
	std::filesystem::path output = source;

	output.replace_extension();
	if (output.empty() || output == source) {
		output = source;
		output += ".out";
	}
	return output.string();
}

std::string latexPdfOutputPathForSource(const std::string &sourcePath) {
	std::filesystem::path output(sourcePath);

	output.replace_extension(".pdf");
	return output.string();
}

std::string directoryPathForSource(const std::string &sourcePath) {
	std::filesystem::path path(sourcePath);
	std::filesystem::path parent = path.parent_path();

	return parent.empty() ? std::string(".") : parent.string();
}

std::string fileNameForSource(const std::string &sourcePath) {
	return std::filesystem::path(sourcePath).filename().string();
}

std::string sourceStemForPath(const std::string &sourcePath) {
	return std::filesystem::path(sourcePath).stem().string();
}

bool pathIsDirectory(const std::string &path) {
	std::error_code error;

	return std::filesystem::is_directory(path, error);
}

std::string trimShellCommand(std::string_view command) {
	std::size_t start = 0;
	std::size_t end = command.size();

	while (start < end && std::isspace(static_cast<unsigned char>(command[start])) != 0)
		++start;
	while (end > start && std::isspace(static_cast<unsigned char>(command[end - 1])) != 0)
		--end;
	return std::string(command.substr(start, end - start));
}

void appendShellCommandGroup(std::ostringstream &out, const std::string &command) {
	out << "( " << command << " )";
}

void appendShellVariableAssignment(std::ostringstream &out, const char *name, const std::string &value) {
	out << name << "=" << shellQuote(value) << "; export " << name << "; ";
}

void appendBuildShellContext(std::ostringstream &out, const MRBuildHookContext &context) {
	appendShellVariableAssignment(out, "MR_BUILD_SOURCE_PATH", context.sourcePath);
	appendShellVariableAssignment(out, "MR_BUILD_SOURCE_DIR", context.sourceDir);
	appendShellVariableAssignment(out, "MR_BUILD_SOURCE_FILE", context.sourceFile);
	appendShellVariableAssignment(out, "MR_BUILD_SOURCE_STEM", context.sourceStem);
	appendShellVariableAssignment(out, "MR_BUILD_OUTPUT_PATH", context.outputPath);
	appendShellVariableAssignment(out, "MR_BUILD_PDF_PATH", context.pdfPath);
	appendShellVariableAssignment(out, "MR_BUILD_PROFILE_ID", context.profileId);
	appendShellVariableAssignment(out, "MR_BUILD_PROFILE_NAME", context.profileName);
	appendShellVariableAssignment(out, "MR_BUILD_TOOLCHAIN", context.toolchain);
	appendShellVariableAssignment(out, "MR_BUILD_SOURCE_BUFFER_ID", std::to_string(context.sourceBufferId));
}

std::string wrapBuildCommandWithProfileHooks(const MRCompilerProfile &profile, const std::string &buildCommand, const MRBuildHookContext &context) {
	const std::string preCommand = trimShellCommand(profile.preBuildCommand);
	const std::string succeededCommand = trimShellCommand(profile.buildSucceededCommand);
	const std::string failedCommand = trimShellCommand(profile.buildFailedCommand);
	std::ostringstream command;

	if (preCommand.empty() && succeededCommand.empty() && failedCommand.empty()) return buildCommand;
	appendBuildShellContext(command, context);
	command << "__mr_build_status=0; ";
	if (!preCommand.empty()) {
		appendShellCommandGroup(command, preCommand);
		command << "; __mr_pre_status=$?; if [ \"$__mr_pre_status\" -eq 0 ]; then ";
	} else
		command << "if true; then ";
	appendShellCommandGroup(command, buildCommand);
	command << "; __mr_build_status=$?; ";
	if (!preCommand.empty()) command << "else __mr_build_status=$__mr_pre_status; fi; ";
	else
		command << "fi; ";
	command << "if [ \"$__mr_build_status\" -eq 0 ]; then ";
	if (!succeededCommand.empty()) appendShellCommandGroup(command, succeededCommand);
	else
		command << ":";
	command << "; else ";
	if (!failedCommand.empty()) appendShellCommandGroup(command, failedCommand);
	else
		command << ":";
	command << "; fi; exit \"$__mr_build_status\"";
	return command.str();
}

std::shared_ptr<mr::coprocessor::ExternalIoFinishedPayload> makeFinishedPayload(std::size_t channelId, int exitCode, bool signaled, int signalNumber, const MRBuildHookContext &context, const std::string &successAudioUri, const std::string &failureAudioUri) {
	std::shared_ptr<mr::coprocessor::ExternalIoFinishedPayload> payload = std::make_shared<mr::coprocessor::ExternalIoFinishedPayload>(channelId, exitCode, signaled, signalNumber, 0, successAudioUri, failureAudioUri);

	payload->buildSourcePath = context.sourcePath;
	payload->buildSourceDir = context.sourceDir;
	payload->buildSourceFile = context.sourceFile;
	payload->buildSourceStem = context.sourceStem;
	payload->buildOutputPath = context.outputPath;
	payload->buildPdfPath = context.pdfPath;
	payload->buildProfileId = context.profileId;
	payload->buildProfileName = context.profileName;
	payload->buildToolchain = context.toolchain;
	payload->postBuildMacro = context.postBuildMacro;
	return payload;
}
} // namespace

std::string shortenCommandTitle(std::string_view command) {
	std::string trimmed = trimPathInput(command);

	if (trimmed.empty()) trimmed = "(empty)";
	if (trimmed.size() > 54) trimmed = trimmed.substr(0, 51) + "...";
	return "CMD: " + trimmed;
}

MRBuildHookContext buildCompilerProfileHookContext(const MRCompilerProfile &profile, const std::string &sourcePath, int sourceBufferId) {
	MRBuildHookContext context;
	const std::string source = trimPathInput(sourcePath);

	context.sourcePath = source;
	context.sourceDir = directoryPathForSource(source);
	context.sourceFile = fileNameForSource(source);
	context.sourceStem = sourceStemForPath(source);
	context.outputPath = profile.toolchain == "LATEXMK" || profile.toolchain == "LATEX" ? latexPdfOutputPathForSource(source) : compilerOutputPathForSource(source);
	context.pdfPath = latexPdfOutputPathForSource(source);
	context.profileId = profile.id;
	context.profileName = profile.name;
	context.toolchain = profile.toolchain;
	context.preBuildMacro = profile.preBuildMacro;
	context.postBuildMacro = profile.postBuildMacro;
	context.sourceBufferId = sourceBufferId;
	return context;
}

void applyBuildHookContextGlobals(const MRBuildHookContext &context, int exitStatus, const std::string &statusText, const std::string &errorText) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();

	mrvmRuntimeGlobalWrite(runtimeKv, "MR_BUILD_SOURCE_PATH", TYPE_STR, mrvmMakeString(context.sourcePath));
	mrvmRuntimeGlobalWrite(runtimeKv, "MR_BUILD_SOURCE_DIR", TYPE_STR, mrvmMakeString(context.sourceDir));
	mrvmRuntimeGlobalWrite(runtimeKv, "MR_BUILD_SOURCE_FILE", TYPE_STR, mrvmMakeString(context.sourceFile));
	mrvmRuntimeGlobalWrite(runtimeKv, "MR_BUILD_SOURCE_STEM", TYPE_STR, mrvmMakeString(context.sourceStem));
	mrvmRuntimeGlobalWrite(runtimeKv, "MR_BUILD_OUTPUT_PATH", TYPE_STR, mrvmMakeString(context.outputPath));
	mrvmRuntimeGlobalWrite(runtimeKv, "MR_BUILD_PDF_PATH", TYPE_STR, mrvmMakeString(context.pdfPath));
	mrvmRuntimeGlobalWrite(runtimeKv, "MR_BUILD_PROFILE_ID", TYPE_STR, mrvmMakeString(context.profileId));
	mrvmRuntimeGlobalWrite(runtimeKv, "MR_BUILD_PROFILE_NAME", TYPE_STR, mrvmMakeString(context.profileName));
	mrvmRuntimeGlobalWrite(runtimeKv, "MR_BUILD_TOOLCHAIN", TYPE_STR, mrvmMakeString(context.toolchain));
	mrvmRuntimeGlobalWrite(runtimeKv, "MR_BUILD_SOURCE_BUFFER_ID", TYPE_INT, mrvmMakeInt(context.sourceBufferId));
	mrvmRuntimeGlobalWrite(runtimeKv, "MR_BUILD_EXIT_STATUS", TYPE_INT, mrvmMakeInt(exitStatus));
	mrvmRuntimeGlobalWrite(runtimeKv, "MR_BUILD_STATUS_TEXT", TYPE_STR, mrvmMakeString(statusText));
	mrvmRuntimeGlobalWrite(runtimeKv, "MR_BUILD_ERROR_TEXT", TYPE_STR, mrvmMakeString(errorText));
}

bool runBuildHookMacro(const std::string &macroSpec, const MRBuildHookContext &context, int exitStatus, const std::string &statusText, const std::string &errorText, std::string *errorMessage) {
	std::string macroError;
	const std::string spec = trimAscii(macroSpec);

	if (errorMessage != nullptr) errorMessage->clear();
	if (spec.empty()) return true;
	applyBuildHookContextGlobals(context, exitStatus, statusText, errorText);
	if (!runMacroSpecByName(spec.c_str(), &macroError, false)) {
		if (errorMessage != nullptr) *errorMessage = macroError.empty() ? std::string("Build hook macro failed.") : macroError;
		return false;
	}
	return true;
}

bool buildCompilerProfileCommandLine(const MRCompilerProfile &profile, const std::string &sourcePath, std::string &commandLine, std::string *errorMessage) {
	std::string toolchain = profile.toolchain;
	std::string source = trimPathInput(sourcePath);
	MRBuildHookContext context = buildCompilerProfileHookContext(profile, sourcePath);
	std::ostringstream command;
	std::string buildCommand;

	commandLine.clear();
	if (source.empty()) return setError(errorMessage, "No source file selected for build.");
	if (profile.executablePath.empty()) return setError(errorMessage, "Compiler profile has no executable path.");
	if (toolchain != "GCC" && toolchain != "CLANG" && toolchain != "SWIFT" && toolchain != "LATEXMK" && toolchain != "LATEX") return setError(errorMessage, "Build current file currently supports GCC, CLANG, SWIFT, LATEXMK and LATEX compiler profiles.");

	command << shellQuote(profile.executablePath);
	if (!profile.buildFlags.empty()) command << ' ' << profile.buildFlags;
	if (toolchain == "LATEXMK") {
		command << ' ' << shellQuote(source);
		commandLine = wrapBuildCommandWithProfileHooks(profile, command.str(), context);
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (toolchain == "LATEX") {
		command << ' ' << shellQuote(source);
		commandLine = wrapBuildCommandWithProfileHooks(profile, command.str(), context);
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	for (const std::string &path : profile.includePaths)
		if (!path.empty()) command << " -I" << shellQuote(path);
	for (const std::string &path : profile.libraryPaths)
		if (!path.empty()) command << " -L" << shellQuote(path);
	if (toolchain == "SWIFT")
		for (const std::string &path : profile.runtimePaths)
			if (!path.empty() && pathIsDirectory(path)) command << " -Xlinker -rpath -Xlinker " << shellQuote(path);
	command << ' ' << shellQuote(source);
	command << " -o " << shellQuote(compilerOutputPathForSource(source));

	buildCommand = command.str();
	commandLine = wrapBuildCommandWithProfileHooks(profile, buildCommand, context);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

mr::coprocessor::Result runExternalCommandTask(const mr::coprocessor::TaskInfo &info, std::stop_token stopToken, std::size_t channelId, const std::string &command, const MRBuildHookContext &buildContext, const std::string &successAudioUri, const std::string &failureAudioUri) {
	mr::coprocessor::Result result;
	int pipeFds[2] = {-1, -1};
	pid_t childPid = -1;
	int waitStatus = 0;
	int readFlags;
	bool childExited = false;
	bool pipeOpen = true;
	bool cancellationRequested = false;
	int stopPolls = 0;
	std::array<char, 4096> buffer{};

	result.task = info;
	if (::pipe(pipeFds) != 0) {
		result.status = mr::coprocessor::TaskStatus::Failed;
		result.error = std::string("pipe failed: ") + std::strerror(errno);
		result.payload = makeFinishedPayload(channelId, -1, false, 0, buildContext, successAudioUri, failureAudioUri);
		return result;
	}

	childPid = ::fork();
	if (childPid < 0) {
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		result.status = mr::coprocessor::TaskStatus::Failed;
		result.error = std::string("fork failed: ") + std::strerror(errno);
		result.payload = makeFinishedPayload(channelId, -1, false, 0, buildContext, successAudioUri, failureAudioUri);
		return result;
	}

	if (childPid == 0) {
		std::string shellPath = configuredShellExecutablePath();
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
	readFlags = ::fcntl(pipeFds[0], F_GETFL, 0);
	if (readFlags >= 0) ::fcntl(pipeFds[0], F_SETFL, readFlags | O_NONBLOCK);

	while (pipeOpen || !childExited) {
		struct pollfd pfd;
		int pollResult;

		if ((stopToken.stop_requested() || info.cancelRequested()) && !childExited) {
			cancellationRequested = true;
			if (stopPolls == 0) ::kill(-childPid, SIGTERM);
			else if (stopPolls > 10)
				::kill(-childPid, SIGKILL);
			++stopPolls;
		}

		pfd.fd = pipeFds[0];
		pfd.events = POLLIN | POLLHUP;
		pfd.revents = 0;
		pollResult = pipeOpen ? ::poll(&pfd, 1, 100) : 0;
		if (pollResult < 0 && errno != EINTR) {
			result.status = mr::coprocessor::TaskStatus::Failed;
			result.error = std::string("poll failed: ") + std::strerror(errno);
			break;
		}

		if (pipeOpen && (pollResult > 0 || childExited)) {
			for (;;) {
				ssize_t count = ::read(pipeFds[0], buffer.data(), buffer.size());
				if (count > 0) {
					mr::coprocessor::Result chunkResult;
					chunkResult.task = info;
					chunkResult.status = mr::coprocessor::TaskStatus::Completed;
					chunkResult.payload = std::make_shared<mr::coprocessor::ExternalIoChunkPayload>(channelId, std::string(buffer.data(), static_cast<std::size_t>(count)));
					mr::coprocessor::globalCoprocessor().post(std::move(chunkResult));
					continue;
				}
				if (count == 0) {
					pipeOpen = false;
					break;
				}
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
				result.status = mr::coprocessor::TaskStatus::Failed;
				result.error = std::string("read failed: ") + std::strerror(errno);
				pipeOpen = false;
				break;
			}
			if (result.failed()) break;
		}

		if (!childExited) {
			pid_t waited = ::waitpid(childPid, &waitStatus, WNOHANG);
			if (waited == childPid) childExited = true;
			else if (waited < 0 && errno != EINTR) {
				result.status = mr::coprocessor::TaskStatus::Failed;
				result.error = std::string("waitpid failed: ") + std::strerror(errno);
				break;
			}
		}
	}

	if (pipeFds[0] >= 0) ::close(pipeFds[0]);
	if (!childExited && childPid > 0) {
		while (::waitpid(childPid, &waitStatus, 0) < 0 && errno == EINTR)
			;
		childExited = true;
	}

	if (result.failed()) {
		result.payload = makeFinishedPayload(channelId, childExited && WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : -1, childExited && WIFSIGNALED(waitStatus) != 0, childExited && WIFSIGNALED(waitStatus) ? WTERMSIG(waitStatus) : 0, buildContext, successAudioUri, failureAudioUri);
		return result;
	}
	if (cancellationRequested || stopToken.stop_requested() || info.cancelRequested()) {
		result.status = mr::coprocessor::TaskStatus::Cancelled;
		result.payload = makeFinishedPayload(channelId, childExited && WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : -2, childExited && WIFSIGNALED(waitStatus) != 0, childExited && WIFSIGNALED(waitStatus) ? WTERMSIG(waitStatus) : 0, buildContext, successAudioUri, failureAudioUri);
		return result;
	}

	result.status = mr::coprocessor::TaskStatus::Completed;
	result.payload = makeFinishedPayload(channelId, WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : -1, WIFSIGNALED(waitStatus) != 0, WIFSIGNALED(waitStatus) ? WTERMSIG(waitStatus) : 0, buildContext, successAudioUri, failureAudioUri);
	return result;
}
