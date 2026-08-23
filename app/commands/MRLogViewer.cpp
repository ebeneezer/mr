#define Uses_TApplication
#define Uses_TButton
#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TProgram
#define Uses_TScrollBar
#define Uses_MsgBox
#include <tvision/tv.h>

#include "MRLogViewer.hpp"

#include "MRFileCommands.hpp"
#include "MRWindowCommands.hpp"
#include "../MRHelpTopics.generated.hpp"
#include "../router/MRCommandRouterSearch.hpp"
#include "../router/MRCommandRouterSearchCore.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../coprocessor/MRCoprocessor.hpp"
#include "../../ui/widgets/MRColumnListView.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/widgets/MRDropList.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../../dialogs/setup/MRSetupCommon.hpp"

#include <algorithm>
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
#include <vector>

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

constexpr ushort cmJournalTagHistory = 0x7350;
constexpr ushort cmJournalTagHistoryAccept = 0x7351;
constexpr ushort cmJournalTagSelectionChanged = 0x7352;
constexpr ushort cmJournalTagSelectionAccepted = 0x7353;

std::vector<std::string> collectJournalSyslogIdentifiers() {
	std::vector<std::string> identifiers;
	int pipeFds[2] = {-1, -1};
	pid_t pid = -1;
	std::string output;
	char buffer[4096];

	if (::pipe(pipeFds) != 0) return identifiers;
	pid = ::fork();
	if (pid == 0) {
		::close(pipeFds[0]);
		if (::dup2(pipeFds[1], STDOUT_FILENO) < 0) _exit(127);
		::close(pipeFds[1]);
		::execlp("journalctl", "journalctl", "-F", "SYSLOG_IDENTIFIER", static_cast<char *>(nullptr));
		_exit(127);
	}
	::close(pipeFds[1]);
	pipeFds[1] = -1;
	if (pid < 0) {
		::close(pipeFds[0]);
		return identifiers;
	}
	for (;;) {
		const ssize_t got = ::read(pipeFds[0], buffer, sizeof(buffer));
		if (got > 0) {
			output.append(buffer, static_cast<std::size_t>(got));
			continue;
		}
		if (got == 0) break;
		if (errno == EINTR) continue;
		break;
	}
	::close(pipeFds[0]);
	static_cast<void>(::waitpid(pid, nullptr, 0));

	std::istringstream lines(output);
	for (std::string line; std::getline(lines, line);) {
		line = trimInput(line);
		if (line.empty()) continue;
		if (std::find(identifiers.begin(), identifiers.end(), line) == identifiers.end()) identifiers.push_back(line);
	}
	std::sort(identifiers.begin(), identifiers.end());
	return identifiers;
}

class JournalTagDialog : public MRDialogFoundation {
  public:
	JournalTagDialog(const std::vector<std::string> &historyValues, const std::vector<std::string> &identifierValues)
	    : TWindowInit(initFrame), MRDialogFoundation(mr::dialogs::centeredDialogRect(62, 18), "OPEN JOURNAL", 62, 18, initFrame), tagField(nullptr), history(historyValues), identifiers(identifierValues) {
		helpCtx = hcDialogJournalTag;
		tagField = new TInputLine(TRect(14, 3, 51, 4), 127);
		insert(new TLabel(TRect(3, 3, 13, 4), "App ~t~ag:", tagField));
		insert(tagField);
		historyButton = historyDropList.createButton(*this, TRect(51, 3, 54, 4), tagField, this, cmJournalTagHistory, true);
		insert(new TLabel(TRect(3, 5, 24, 6), "Journal identifiers:", nullptr));
		identifierScrollBar = new TScrollBar(TRect(55, 6, 56, 14));
		insert(identifierScrollBar);
		identifierList = new MRColumnListView(TRect(3, 6, 55, 14), identifierScrollBar, this, cmJournalTagSelectionChanged, cmJournalTagSelectionAccepted);
		insert(identifierList);
		setIdentifierRows();
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~O~K", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 2, 10);
		mr::dialogs::insertUniformButtonRow(*this, (62 - metrics.rowWidth) / 2, 15, 2, buttons, 10);
		tagField->select();
	}

	void handleEvent(TEvent &event) override {
		if (historyDropList.handleLinkedInputEvent(event, *this, TRect(14, 4, 54, 5), history, tagField, this, cmJournalTagHistoryAccept, 8)) return;
		if (event.what == evCommand) {
			if (event.message.command == cmJournalTagHistory) {
				historyDropList.toggle(*this, TRect(14, 4, 54, 5), history, currentTag(), this, cmJournalTagHistoryAccept, 8);
				clearEvent(event);
				return;
			}
			if (event.message.command == cmJournalTagHistoryAccept) {
				acceptDropListValue(historyDropList);
				clearEvent(event);
				return;
			}
			if (event.message.command == cmJournalTagSelectionAccepted) {
				acceptIdentifierSelection();
				endModal(cmOK);
				clearEvent(event);
				return;
			}
		}
		if (event.what == evBroadcast && event.message.command == cmJournalTagSelectionChanged) {
			acceptIdentifierSelection();
			clearEvent(event);
			return;
		}

		MRDialogFoundation::handleEvent(event);
	}

	std::string selectedTag() const {
		return currentTag();
	}

  private:
	std::string currentTag() const {
		std::array<char, 128> buffer{};

		if (tagField == nullptr) return std::string();
		tagField->getData(buffer.data());
		return trimInput(buffer.data());
	}

	void setCurrentTag(const std::string &value) {
		if (tagField == nullptr) return;
		tagField->setData(const_cast<char *>(value.c_str()));
		tagField->select();
	}

	void setCurrentTagFromList(const std::string &value) {
		if (tagField == nullptr) return;
		tagField->setData(const_cast<char *>(value.c_str()));
	}

	void acceptDropListValue(MRDropList &dropList) {
		std::string selected;

		if (!dropList.acceptSelection(selected)) return;
		setCurrentTag(selected);
		historyDropList.hide();
	}

	void setIdentifierRows() {
		std::vector<MRColumnListView::Row> rows;

		if (identifierList == nullptr) return;
		rows.reserve(identifiers.size());
		for (const std::string &identifier : identifiers)
			rows.push_back(MRColumnListView::Row{identifier});
		identifierList->setRows(rows, 0);
	}

	void acceptIdentifierSelection() {
		short selection = -1;

		if (identifierList == nullptr) return;
		selection = identifierList->selectedIndex();
		if (selection < 0 || static_cast<std::size_t>(selection) >= identifiers.size()) return;
		setCurrentTagFromList(identifiers[static_cast<std::size_t>(selection)]);
		historyDropList.hide();
	}

	TInputLine *tagField;
	TView *historyButton = nullptr;
	TScrollBar *identifierScrollBar = nullptr;
	MRColumnListView *identifierList = nullptr;
	MRDropList historyDropList;
	std::vector<std::string> history;
	std::vector<std::string> identifiers;
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

mr::coprocessor::Result runFileTailTask(const mr::coprocessor::TaskInfo &info, std::size_t sourceId, std::size_t targetBufferId, const std::string &path, LogSearchSnapshot search, MRLiveLogSettings liveLogSettings) {
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
	result.payload = std::make_shared<mr::coprocessor::ExternalIoFinishedPayload>(sourceId, -1, false, 0, targetBufferId);
	if (!search.pattern.empty()) compileSearchRegex(buildSearchPatternExpression(search.pattern, search.options.textType), !search.options.caseSensitive, &searchCode, regexError);
	while (!info.cancelRequested()) {
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
			if (info.cancelRequested()) break;
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
		for (int i = 0; i < 5 && !info.cancelRequested(); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	if (fd >= 0) ::close(fd);
	if (searchCode != nullptr) pcre2_code_free(searchCode);
	result.status = mr::coprocessor::TaskStatus::Cancelled;
	return result;
}

mr::coprocessor::Result runJournalTask(const mr::coprocessor::TaskInfo &info, std::size_t sourceId, std::size_t targetBufferId, const std::string &appTag, LogSearchSnapshot search, MRLiveLogSettings liveLogSettings) {
	mr::coprocessor::Result result;
	pcre2_code *searchCode = nullptr;
	std::string regexError;
	int pipeFds[2] = {-1, -1};
	pid_t childPid = -1;
	int waitStatus = 0;
	int readFlags = -1;
	bool childExited = false;
	bool pipeOpen = true;
	int stopPolls = 0;
	bool lineStart = true;
	std::array<char, 4096> buffer{};

	result.task = info;
	result.payload = std::make_shared<mr::coprocessor::ExternalIoFinishedPayload>(sourceId, -1, false, 0, targetBufferId);
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
	readFlags = ::fcntl(pipeFds[0], F_GETFL, 0);
	if (readFlags < 0 || ::fcntl(pipeFds[0], F_SETFL, readFlags | O_NONBLOCK) < 0) {
		result.status = mr::coprocessor::TaskStatus::Failed;
		result.error = std::string("fcntl failed: ") + std::strerror(errno);
	}
	while (!result.failed() && (pipeOpen || !childExited)) {
		struct pollfd pfd;
		int pollResult;

		if (info.cancelRequested() && (pipeOpen || !childExited)) {
			if (stopPolls == 0) {
				if (::kill(-childPid, SIGTERM) != 0 && !childExited) ::kill(childPid, SIGTERM);
			} else if (stopPolls > 10) {
				if (::kill(-childPid, SIGKILL) != 0 && !childExited) ::kill(childPid, SIGKILL);
				break;
			}
			++stopPolls;
		}
		if (info.cancelRequested() && pipeOpen) {
			::close(pipeFds[0]);
			pipeFds[0] = -1;
			pipeOpen = false;
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
				if (info.cancelRequested()) {
					::close(pipeFds[0]);
					pipeFds[0] = -1;
					pipeOpen = false;
					break;
				}
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
		if (!pipeOpen && !childExited) std::this_thread::sleep_for(std::chrono::milliseconds(100));
		if (!childExited && !info.cancelRequested() && !result.failed()) {
			pid_t waited = ::waitpid(childPid, &waitStatus, WNOHANG);
			if (waited == childPid) childExited = true;
			else if (waited < 0 && errno != EINTR) {
				result.status = mr::coprocessor::TaskStatus::Failed;
				result.error = std::string("waitpid failed: ") + std::strerror(errno);
				break;
			}
		}
	}

	if (info.cancelRequested() && childPid > 0 && ::kill(-childPid, SIGKILL) != 0 && !childExited) ::kill(childPid, SIGKILL);
	if (pipeFds[0] >= 0) ::close(pipeFds[0]);
	if (!childExited && childPid > 0 && result.failed() && ::kill(-childPid, SIGKILL) != 0) ::kill(childPid, SIGKILL);
	if (!childExited && childPid > 0) {
		while (::waitpid(childPid, &waitStatus, 0) < 0 && errno == EINTR)
			;
	}
	if (result.failed()) {
		if (searchCode != nullptr) pcre2_code_free(searchCode);
		return result;
	}
	if (info.cancelRequested()) {
		if (searchCode != nullptr) pcre2_code_free(searchCode);
		result.status = mr::coprocessor::TaskStatus::Cancelled;
		return result;
	}
	if (searchCode != nullptr) pcre2_code_free(searchCode);
	result.status = mr::coprocessor::TaskStatus::Completed;
	result.payload = std::make_shared<mr::coprocessor::ExternalIoFinishedPayload>(sourceId, WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : -1, WIFSIGNALED(waitStatus) != 0, WIFSIGNALED(waitStatus) ? WTERMSIG(waitStatus) : 0, targetBufferId);
	return result;
}

bool startLogViewerWindow(MREditWindow *win, mr::coprocessor::ExternalSourceKind sourceKind, const std::string &sourceName, const std::string &title, const MRLiveLogSettings &liveLogSettings, mr::coprocessor::TaskFn taskFn) {
	std::size_t sourceId;
	std::uint64_t taskId;

	if (win == nullptr) return false;
	win->setDisplayTitle(title.c_str());
	win->setLogViewerOptions(liveLogSettings.showLineNumbers, liveLogSettings.scrollDirection);
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

	if (!promptForPath(MRDialogHistoryScope::LiveLogOpen, "OPEN LIVE LOG", fileName, sizeof(fileName))) return true;
	if (!resolveReadableExistingPath(MRDialogHistoryScope::LiveLogOpen, fileName, resolvedPath)) {
		forgetLoadDialogPath(MRDialogHistoryScope::LiveLogOpen, fileName);
		return true;
	}
	rememberLoadDialogPath(MRDialogHistoryScope::LiveLogOpen, resolvedPath.c_str());
	currentSearchPatternSnapshot(search.pattern, search.options);

	title = "LIVELOG: " + baseNameOf(resolvedPath);
	win = createCommunicationWindow(title.c_str());
	if (win == nullptr) {
		postLogViewerError("Unable to create live log window.");
		return true;
	}
	if (!win->loadFromFile(resolvedPath.c_str())) {
		message(win, evCommand, cmClose, nullptr);
		postLogViewerError("Unable to load live log content.");
		return true;
	}
	win->setLogViewerOptions(liveLogSettings.showLineNumbers, liveLogSettings.scrollDirection);
	if (liveLogSettings.scrollDirection == MRLiveLogScrollDirection::Up) message(win, evCommand, cmTextStart, nullptr);
	else
		message(win, evCommand, cmTextEnd, nullptr);
	const std::size_t targetBufferId = static_cast<std::size_t>(win->bufferId());
	if (!startLogViewerWindow(win, mr::coprocessor::ExternalSourceKind::File, resolvedPath, title, liveLogSettings, [resolvedPath, targetBufferId, search, liveLogSettings](const mr::coprocessor::TaskInfo &info) { return runFileTailTask(info, info.documentId, targetBufferId, resolvedPath, search, liveLogSettings); })) {
		message(win, evCommand, cmClose, nullptr);
		postLogViewerError("Unable to start live log worker.");
	}
	return true;
}

bool openJournalViewer() {
	std::string appTag;
	MREditWindow *win;
	std::string title;
	LogSearchSnapshot search;
	MRLiveLogSettings liveLogSettings = configuredLiveLogSettings();
	JournalTagDialog *dialog = new JournalTagDialog(liveLogSettings.journalAppTagHistory, collectJournalSyslogIdentifiers());

	if (TProgram::deskTop == nullptr) {
		TObject::destroy(dialog);
		return true;
	}
	dialog->finalizeLayout();
	if (TProgram::deskTop->execView(dialog) == cmCancel) {
		TObject::destroy(dialog);
		return true;
	}
	appTag = dialog->selectedTag();
	TObject::destroy(dialog);
	if (appTag.empty()) {
		postLogViewerError("No journal app tag specified.");
		return true;
	}
	liveLogSettings.journalAppTagHistory.erase(std::remove(liveLogSettings.journalAppTagHistory.begin(), liveLogSettings.journalAppTagHistory.end(), appTag), liveLogSettings.journalAppTagHistory.end());
	liveLogSettings.journalAppTagHistory.insert(liveLogSettings.journalAppTagHistory.begin(), appTag);
	static_cast<void>(setConfiguredLiveLogSettings(liveLogSettings));
	currentSearchPatternSnapshot(search.pattern, search.options);

	title = "JOURNAL: " + appTag;
	win = createCommunicationWindow(title.c_str());
	if (win == nullptr) {
		postLogViewerError("Unable to create journal window.");
		return true;
	}
	const std::size_t targetBufferId = static_cast<std::size_t>(win->bufferId());
	if (!startLogViewerWindow(win, mr::coprocessor::ExternalSourceKind::Journal, appTag, title, liveLogSettings, [appTag, targetBufferId, search, liveLogSettings](const mr::coprocessor::TaskInfo &info) { return runJournalTask(info, info.documentId, targetBufferId, appTag, search, liveLogSettings); })) {
		message(win, evCommand, cmClose, nullptr);
		postLogViewerError("Unable to start journal worker.");
	}
	return true;
}
