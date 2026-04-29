#define Uses_MsgBox
#include <tvision/tv.h>

#include "MRExternalCommand.hpp"
#include "MRDialogPaths.hpp"

#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

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
} // namespace

bool promptForCommandLine(std::string &commandLine) {
	constexpr std::size_t CommandBufferSize = 256;
	std::array<char, CommandBufferSize> command{};
	uchar limit;

	commandLine.clear();
	limit = static_cast<uchar>(command.size() - 1);
	if (inputBox("EXECUTE PROGRAM", "~C~ommand", command.data(), limit) == cmCancel) return false;
	commandLine = trimPathInput(command.data());
	return true;
}

std::string shortenCommandTitle(std::string_view command) {
	std::string trimmed = trimPathInput(command);

	if (trimmed.empty()) trimmed = "(empty)";
	if (trimmed.size() > 54) trimmed = trimmed.substr(0, 51) + "...";
	return "CMD: " + trimmed;
}

mr::coprocessor::Result runExternalCommandTask(const mr::coprocessor::TaskInfo &info, std::stop_token stopToken, std::size_t channelId, const std::string &command) {
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
		return result;
	}

	childPid = ::fork();
	if (childPid < 0) {
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		result.status = mr::coprocessor::TaskStatus::Failed;
		result.error = std::string("fork failed: ") + std::strerror(errno);
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

	if (result.failed()) return result;
	if (cancellationRequested || stopToken.stop_requested() || info.cancelRequested()) {
		result.status = mr::coprocessor::TaskStatus::Cancelled;
		return result;
	}

	result.status = mr::coprocessor::TaskStatus::Completed;
	result.payload = std::make_shared<mr::coprocessor::ExternalIoFinishedPayload>(channelId, WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : -1, WIFSIGNALED(waitStatus) != 0, WIFSIGNALED(waitStatus) ? WTERMSIG(waitStatus) : 0);
	return result;
}
