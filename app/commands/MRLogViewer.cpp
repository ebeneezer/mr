#define Uses_TApplication
#define Uses_MsgBox
#include <tvision/tv.h>

#include "MRLogViewer.hpp"

#include "MRFileCommands.hpp"
#include "MRWindowCommands.hpp"
#include "../router/MRCommandRouterSearch.hpp"
#include "../router/MRCommandRouterSearchCore.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../coprocessor/MRCoprocessor.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MRWindowSupport.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

std::string trimInput(std::string_view text) {
	std::size_t start = 0;
	std::size_t end = text.size();

	while (start < end && static_cast<unsigned char>(text[start]) <= 32)
		++start;
	while (end > start && static_cast<unsigned char>(text[end - 1]) <= 32)
		--end;
	return std::string(text.substr(start, end - start));
}

std::string baseNameOf(std::string_view path) {
	const std::size_t pos = path.find_last_of("\\/");
	if (pos == std::string_view::npos) return std::string(path);
	return std::string(path.substr(pos + 1));
}

void postLogViewerError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string(text), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

struct LogSearchSnapshot {
	std::string pattern;
	MRSearchDialogOptions options;
};

std::string currentTimeStampPrefix() {
	char buffer[32];
	std::time_t now = std::time(nullptr);
	std::tm local {};

	if (localtime_r(&now, &local) == nullptr || std::strftime(buffer, sizeof(buffer), "[%H:%M:%S] ", &local) == 0) return "[--:--:--] ";
	return buffer;
}

std::string decorateLogChunk(std::string_view text, bool timestamps, bool &lineStart) {
	std::string decorated;

	if (!timestamps) return std::string(text);
	decorated.reserve(text.size() + 16);
	for (char ch : text) {
		if (lineStart) {
			decorated += currentTimeStampPrefix();
			lineStart = false;
		}
		decorated.push_back(ch);
		if (ch == '\n') lineStart = true;
	}
	return decorated;
}

std::size_t countSearchHits(pcre2_code *code, std::string_view text) {
	std::vector<SearchMatchEntry> matches;
	std::string chunkText;

	if (code == nullptr || text.empty()) return 0;
	chunkText.assign(text.data(), text.size());
	if (!collectRegexMatches(chunkText, code, matches)) return 0;
	return matches.size();
}

void postExternalChunk(const mr::coprocessor::TaskInfo &info, std::size_t sourceId, std::size_t targetBufferId, std::string_view text, pcre2_code *searchCode, bool reportSearchHits) {
	mr::coprocessor::Result chunkResult;
	std::size_t searchHitCount = 0;

	if (text.empty()) return;
	if (reportSearchHits) searchHitCount = countSearchHits(searchCode, text);
	chunkResult.task = info;
	chunkResult.status = mr::coprocessor::TaskStatus::Completed;
	chunkResult.payload = std::make_shared<mr::coprocessor::ExternalIoChunkPayload>(sourceId, std::string(text), targetBufferId, searchHitCount);
	mr::coprocessor::globalCoprocessor().post(std::move(chunkResult));
}

mr::coprocessor::Result runFileTailTask(const mr::coprocessor::TaskInfo &info, std::stop_token stopToken, std::size_t sourceId, std::size_t targetBufferId, const std::string &path, LogSearchSnapshot search, MRLiveLogSettings liveLogSettings) {
	mr::coprocessor::Result result;
	std::array<char, 8192> buffer{};
	pcre2_code *searchCode = nullptr;
	std::string regexError;
	int fd = -1;
	dev_t device = 0;
	ino_t inode = 0;
	off_t offset = 0;
	bool reportSearchHits = false;
	bool lineStart = true;

	result.task = info;
	if (!search.pattern.empty()) compileSearchRegex(buildSearchPatternExpression(search.pattern, search.options.textType), !search.options.caseSensitive, &searchCode, regexError);
	while (!stopToken.stop_requested() && !info.cancelRequested()) {
		struct stat st {};

		if (fd < 0) {
			fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
			if (fd < 0) {
				result.status = mr::coprocessor::TaskStatus::Failed;
				result.error = std::string("open failed: ") + std::strerror(errno);
				if (searchCode != nullptr) pcre2_code_free(searchCode);
				return result;
			}
			if (::fstat(fd, &st) == 0) {
				device = st.st_dev;
				inode = st.st_ino;
				offset = st.st_size;
			}
			reportSearchHits = true;
		}

		if (::stat(path.c_str(), &st) == 0) {
			if (st.st_dev != device || st.st_ino != inode || st.st_size < offset) {
				::close(fd);
				fd = -1;
				continue;
			}
		}

		for (;;) {
			ssize_t count = ::pread(fd, buffer.data(), buffer.size(), offset);
			if (count > 0) {
				std::string chunkText = decorateLogChunk(std::string_view(buffer.data(), static_cast<std::size_t>(count)), liveLogSettings.showTimestamps, lineStart);
				offset += count;
				postExternalChunk(info, sourceId, targetBufferId, chunkText, searchCode, reportSearchHits);
				continue;
			}
			if (count == 0) break;
			if (errno == EINTR) continue;
			result.status = mr::coprocessor::TaskStatus::Failed;
			result.error = std::string("pread failed: ") + std::strerror(errno);
			if (fd >= 0) ::close(fd);
			if (searchCode != nullptr) pcre2_code_free(searchCode);
			return result;
		}

		reportSearchHits = true;
		for (int i = 0; i < 5 && !stopToken.stop_requested() && !info.cancelRequested(); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	if (fd >= 0) ::close(fd);
	if (searchCode != nullptr) pcre2_code_free(searchCode);
	result.status = mr::coprocessor::TaskStatus::Cancelled;
	return result;
}

mr::coprocessor::Result runJournalTask(const mr::coprocessor::TaskInfo &info, std::stop_token stopToken, std::size_t sourceId, std::size_t targetBufferId, const std::string &appTag, LogSearchSnapshot search, MRLiveLogSettings liveLogSettings) {
	mr::coprocessor::Result result;
	pcre2_code *searchCode = nullptr;
	std::string regexError;
	int pipeFds[2] = {-1, -1};
	pid_t childPid = -1;
	int waitStatus = 0;
	bool childExited = false;
	bool pipeOpen = true;
	int stopPolls = 0;
	bool lineStart = true;
	std::array<char, 4096> buffer{};

	result.task = info;
	if (!search.pattern.empty()) compileSearchRegex(buildSearchPatternExpression(search.pattern, search.options.textType), !search.options.caseSensitive, &searchCode, regexError);
	if (::pipe(pipeFds) != 0) {
		if (searchCode != nullptr) pcre2_code_free(searchCode);
		result.status = mr::coprocessor::TaskStatus::Failed;
		result.error = std::string("pipe failed: ") + std::strerror(errno);
		return result;
	}

	childPid = ::fork();
	if (childPid < 0) {
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		if (searchCode != nullptr) pcre2_code_free(searchCode);
		result.status = mr::coprocessor::TaskStatus::Failed;
		result.error = std::string("fork failed: ") + std::strerror(errno);
		return result;
	}

	if (childPid == 0) {
		::setpgid(0, 0);
		::dup2(pipeFds[1], STDOUT_FILENO);
		::dup2(pipeFds[1], STDERR_FILENO);
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		::execlp("journalctl", "journalctl", "-f", "-n", "0", "-t", appTag.c_str(), static_cast<char *>(nullptr));
		::_exit(127);
	}

	::close(pipeFds[1]);
	::setpgid(childPid, childPid);
	while (pipeOpen || !childExited) {
		struct pollfd pfd;
		int pollResult;

		if ((stopToken.stop_requested() || info.cancelRequested()) && !childExited) {
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
		if (pipeOpen && pollResult > 0) {
			for (;;) {
				ssize_t count = ::read(pipeFds[0], buffer.data(), buffer.size());
				if (count > 0) {
					std::string chunkText = decorateLogChunk(std::string_view(buffer.data(), static_cast<std::size_t>(count)), liveLogSettings.showTimestamps, lineStart);
					postExternalChunk(info, sourceId, targetBufferId, chunkText, searchCode, true);
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
	}
	if (result.failed()) {
		if (searchCode != nullptr) pcre2_code_free(searchCode);
		return result;
	}
	if (stopToken.stop_requested() || info.cancelRequested()) {
		if (searchCode != nullptr) pcre2_code_free(searchCode);
		result.status = mr::coprocessor::TaskStatus::Cancelled;
		return result;
	}
	if (searchCode != nullptr) pcre2_code_free(searchCode);
	result.status = mr::coprocessor::TaskStatus::Completed;
	result.payload = std::make_shared<mr::coprocessor::ExternalIoFinishedPayload>(sourceId, WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : -1, WIFSIGNALED(waitStatus) != 0, WIFSIGNALED(waitStatus) ? WTERMSIG(waitStatus) : 0, targetBufferId);
	return result;
}

bool startLogViewerWindow(MREditWindow *win, mr::coprocessor::ExternalSourceKind sourceKind, const std::string &sourceName, const std::string &title, mr::coprocessor::TaskFn taskFn) {
	std::size_t sourceId;
	std::uint64_t taskId;

	if (win == nullptr) return false;
	win->setDisplayTitle(title.c_str());
	win->setLogViewerOptions(configuredLiveLogSettings().showLineNumbers);
	win->setReadOnly(true);
	win->setFileChanged(false);
	win->setWindowRole(MREditWindow::wrCommunicationPipe, sourceName);
	static_cast<void>(mrActivateEditWindow(win));

	sourceId = mr::coprocessor::globalCoprocessor().registerExternalSource(sourceKind, sourceName);
	taskId = mr::coprocessor::globalCoprocessor().submitExternal(sourceId, title, std::move(taskFn));
	if (taskId == 0) {
		mr::coprocessor::globalCoprocessor().unregisterExternalSource(sourceId);
		return false;
	}
	win->trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::ExternalIo, title);
	return true;
}

} // namespace

bool openLiveLogViewer() {
	char fileName[MAXPATH];
	std::string resolvedPath;
	MREditWindow *win;
	std::string title;
	LogSearchSnapshot search;
	MRLiveLogSettings liveLogSettings = configuredLiveLogSettings();

	if (!promptForPath(MRDialogHistoryScope::LiveLogOpen, "Open Live Log", fileName, sizeof(fileName))) return true;
	if (!resolveReadableExistingPath(MRDialogHistoryScope::LiveLogOpen, fileName, resolvedPath)) {
		forgetLoadDialogPath(MRDialogHistoryScope::LiveLogOpen, fileName);
		return true;
	}
	rememberLoadDialogPath(MRDialogHistoryScope::LiveLogOpen, resolvedPath.c_str());
	currentSearchPatternSnapshot(search.pattern, search.options);

	title = "LIVELOG: " + baseNameOf(resolvedPath);
	win = createEditorWindow(title.c_str());
	if (win == nullptr) {
		postLogViewerError("Unable to create live log window.");
		return true;
	}
	if (!win->loadFromFile(resolvedPath.c_str())) {
		message(win, evCommand, cmClose, nullptr);
		postLogViewerError("Unable to load live log content.");
		return true;
	}
	win->setLogViewerOptions(liveLogSettings.showLineNumbers);
	if (liveLogSettings.scrollDirection == MRLiveLogScrollDirection::Up) message(win, evCommand, cmTextStart, nullptr);
	else
		message(win, evCommand, cmTextEnd, nullptr);
	const std::size_t targetBufferId = static_cast<std::size_t>(win->bufferId());
	if (!startLogViewerWindow(win, mr::coprocessor::ExternalSourceKind::File, resolvedPath, title, [resolvedPath, targetBufferId, search, liveLogSettings](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) { return runFileTailTask(info, stopToken, info.documentId, targetBufferId, resolvedPath, search, liveLogSettings); })) {
		message(win, evCommand, cmClose, nullptr);
		postLogViewerError("Unable to start live log worker.");
	}
	return true;
}

bool openJournalViewer() {
	std::array<char, 128> tagBuffer{};
	std::string appTag;
	MREditWindow *win;
	std::string title;
	LogSearchSnapshot search;
	MRLiveLogSettings liveLogSettings = configuredLiveLogSettings();

	if (inputBox("Open Journal", "App ~t~ag", tagBuffer.data(), static_cast<uchar>(tagBuffer.size() - 1)) == cmCancel) return true;
	appTag = trimInput(tagBuffer.data());
	if (appTag.empty()) {
		postLogViewerError("No journal app tag specified.");
		return true;
	}
	currentSearchPatternSnapshot(search.pattern, search.options);

	title = "JOURNAL: " + appTag;
	win = createEditorWindow(title.c_str());
	if (win == nullptr) {
		postLogViewerError("Unable to create journal window.");
		return true;
	}
	const std::size_t targetBufferId = static_cast<std::size_t>(win->bufferId());
	if (!startLogViewerWindow(win, mr::coprocessor::ExternalSourceKind::Journal, appTag, title, [appTag, targetBufferId, search, liveLogSettings](const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) { return runJournalTask(info, stopToken, info.documentId, targetBufferId, appTag, search, liveLogSettings); })) {
		message(win, evCommand, cmClose, nullptr);
		postLogViewerError("Unable to start journal worker.");
	}
	return true;
}
