#define Uses_TButton
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TListViewer
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TView
#define Uses_MsgBox
#include <tvision/tv.h>

#include "MRAcquireDialog.hpp"

#include "../app/MRCommands.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../ui/MRDropList.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "setup/MRSetupCommon.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

enum : ushort {
	cmMrAcquireChooseHistory = 3880,
	cmMrAcquireAcceptHistory,
	cmMrAcquireExec,
	cmMrAcquireLoadAll,
	cmMrAcquireLoad
};

constexpr int kDialogWidth = 78;
constexpr int kMinDialogHeight = 14;
constexpr int kMaxDialogHeight = 20;
constexpr int kExecButtonLeft = 2;
constexpr int kExecButtonRight = 11;
constexpr int kCommandInputLeft = 13;
constexpr int kListTop = 4;

class TAcquireCommandEnterInterceptor final : public TView {
  public:
	explicit TAcquireCommandEnterInterceptor(TInputLine *commandField) noexcept : TView(TRect(0, 0, 0, 0)), commandField(commandField) {
		options |= ofPreProcess;
		eventMask |= evKeyDown;
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown && event.keyDown.keyCode == kbEnter && commandField != nullptr && (commandField->state & sfFocused) != 0) {
			TEvent commandEvent{};
			commandEvent.what = evCommand;
			commandEvent.message.command = cmMrAcquireExec;
			putEvent(commandEvent);
			clearEvent(event);
			return;
		}
		TView::handleEvent(event);
	}

  private:
	TInputLine *commandField = nullptr;
};

class AcquireListView final : public TListViewer {
  public:
	AcquireListView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> &items) noexcept : TListViewer(bounds, 1, nullptr, scrollBar), items(items) {
		setRange(static_cast<short>(items.size()));
	}

	void updateItems() {
		setRange(static_cast<short>(items.size()));
		if (!items.empty() && focused >= range) focusItemNum(static_cast<short>(range - 1));
		drawView();
	}

	[[nodiscard]] std::string selectedValue() const {
		if (focused < 0 || focused >= range) return std::string();
		const std::size_t index = static_cast<std::size_t>(focused);
		return index < items.size() ? items[index] : std::string();
	}

	void getText(char *dest, short item, short maxLen) override {
		if (dest == nullptr || maxLen <= 0) return;
		dest[0] = '\0';
		if (item < 0 || static_cast<std::size_t>(item) >= items.size()) return;
		strnzcpy(dest, items[static_cast<std::size_t>(item)].c_str(), static_cast<std::size_t>(maxLen) + 1);
	}

	void handleEvent(TEvent &event) override {
		const bool isDoubleClickActivation = event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0 && (event.mouse.eventFlags & meDoubleClick) != 0;
		TView *target = owner != nullptr && owner->owner != nullptr ? owner->owner : owner;

		TListViewer::handleEvent(event);
		if (isDoubleClickActivation && focused >= 0 && focused < range && target != nullptr) {
			message(target, evCommand, cmMrAcquireLoad, nullptr);
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter && focused >= 0 && focused < range && target != nullptr) {
			message(target, evCommand, cmMrAcquireLoad, nullptr);
			clearEvent(event);
		}
	}

  private:
	std::vector<std::string> &items;
};

[[nodiscard]] int computeDialogHeight() {
	if (TProgram::deskTop == nullptr) return kMaxDialogHeight;
	return std::max(kMinDialogHeight, std::min(kMaxDialogHeight, static_cast<int>(TProgram::deskTop->size.y / 2)));
}

[[nodiscard]] MREditWindow *chooseAcquireBackgroundRestoreWindow() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();

	for (MREditWindow *window : windows)
		if (window != nullptr && (window->state & sfVisible) != 0) return window;
	return currentEditWindow();
}

[[nodiscard]] MREditWindow *chooseAcquireTopWindowAfterDismiss() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();

	for (MREditWindow *window : windows)
		if (window != nullptr && (window->state & sfVisible) != 0) return window;
	return nullptr;
}

[[nodiscard]] bool canRestoreAcquireBackgroundWindow(MREditWindow *window) {
	if (window == nullptr || (window->state & sfVisible) == 0) return false;
	const std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	return std::find(windows.begin(), windows.end(), window) != windows.end();
}

[[nodiscard]] std::string decodeAcquirePathCandidate(std::string_view value) {
	std::string trimmed = trimAscii(value);

	if (trimmed.size() >= 2 && ((trimmed.front() == '"' && trimmed.back() == '"') || (trimmed.front() == '\'' && trimmed.back() == '\''))) trimmed = trimmed.substr(1, trimmed.size() - 2);
	if (trimmed.size() >= 16 && trimmed.compare(0, 16, "file://localhost") == 0) return trimmed.substr(16);
	if (trimmed.size() >= 7 && trimmed.compare(0, 7, "file://") == 0) return trimmed.substr(7);
	return trimmed;
}

bool captureShellStdout(const std::string &commandLine, std::string &output) {
	int pipeFds[2] = {-1, -1};
	pid_t childPid = -1;
	int waitStatus = 0;
	std::array<char, 4096> buffer{};
	std::string shellPath = configuredShellExecutablePath();

	output.clear();
	if (commandLine.empty()) return true;
	if (::pipe(pipeFds) != 0) return false;
	childPid = ::fork();
	if (childPid < 0) {
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		return false;
	}
	if (childPid == 0) {
		int nullFd = ::open("/dev/null", O_WRONLY);

		::dup2(pipeFds[1], STDOUT_FILENO);
		if (nullFd >= 0) {
			::dup2(nullFd, STDERR_FILENO);
			::close(nullFd);
		}
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		::execl(shellPath.c_str(), shellPath.c_str(), "-lc", commandLine.c_str(), static_cast<char *>(nullptr));
		::_exit(127);
	}
	::close(pipeFds[1]);
	for (;;) {
		const ssize_t count = ::read(pipeFds[0], buffer.data(), buffer.size());

		if (count > 0) {
			output.append(buffer.data(), static_cast<std::size_t>(count));
			continue;
		}
		if (count == 0) break;
		if (errno == EINTR) continue;
		break;
	}
	::close(pipeFds[0]);
	while (::waitpid(childPid, &waitStatus, 0) < 0 && errno == EINTR)
		;
	return WIFEXITED(waitStatus) != 0;
}

class TAcquireDialog final : public MRDialogFoundation {
 public:
	explicit TAcquireDialog(MRAcquireMode mode, MREditWindow *backgroundRestoreWindow)
	    : TWindowInit(initMrDialogFrame),
	      MRDialogFoundation(mr::dialogs::centeredDialogRect(kDialogWidth, computeDialogHeight()), "ACQUIRE", kDialogWidth, computeDialogHeight(), initMrDialogFrame),
	      mode(mode),
	      backgroundRestoreWindow(backgroundRestoreWindow) {
		static constexpr std::array buttons{
		    mr::dialogs::DialogButtonSpec{"Load ~a~ll", cmMrAcquireLoadAll, bfNormal},
		    mr::dialogs::DialogButtonSpec{"~L~oad", cmMrAcquireLoad, bfDefault},
		    mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal},
		};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 1);
		const int dialogHeight = size.y;
		const int commandButtonRight = kDialogWidth - 2;
		const int historyButtonLeft = commandButtonRight - 3;
		const int buttonTop = dialogHeight - 3;
		const int listBottom = buttonTop - 1;
		const int listRight = kDialogWidth - 3;
		const int listWidth = listRight - 2;
		const int scrollBarLeft = listRight;
		const int buttonLeft = (kDialogWidth - metrics.rowWidth) / 2;
		const MRAcquireSettings settings = configuredAcquireSettings();

		mCommandField = new TInputLine(TRect(kCommandInputLeft, 2, historyButtonLeft, 3), 255);
		insert(mCommandField);
		insert(new TAcquireCommandEnterInterceptor(mCommandField));
		insert(new TButton(TRect(kExecButtonLeft, 2, kExecButtonRight, 4), "~E~xec:", cmMrAcquireExec, bfNormal));
		commandHistoryAnchor = mCommandField->getBounds();
		commandHistoryAnchor.move(1, 1);
		commandHistoryDropList.createButton(*this, TRect(historyButtonLeft, 2, commandButtonRight, 3), mCommandField, this, cmMrAcquireChooseHistory, false);

		mScrollBar = new TScrollBar(TRect(scrollBarLeft, kListTop, scrollBarLeft + 1, listBottom));
		insert(mScrollBar);
		mListView = new AcquireListView(TRect(2, kListTop, 2 + listWidth, listBottom), mScrollBar, resolvedPaths);
		insert(mListView);

		mr::dialogs::insertUniformButtonRow(*this, buttonLeft, buttonTop, 1, buttons, 0, &buttonsRow);
		finalizeLayout();

		commandHistory = settings.commandHistory;
		mr::dialogs::writeRecordField(commandBuffer, sizeof(commandBuffer), settings.commandLine);
		mCommandField->setData(commandBuffer);
		updateButtons();
		mCommandField->select();
	}

	Boolean valid(ushort command) override {
		if (command == cmCancel || command == cmClose) persistSettings();
		return TDialog::valid(command);
	}

	void handleEvent(TEvent &event) override {
		if (commandHistoryDropList.handleLinkedInputEvent(event, *this, commandHistoryAnchor, commandHistory, mCommandField, this, cmMrAcquireAcceptHistory, commandHistoryVisibleRows())) return;
		if (event.what == evNothing && loadPending) {
			performPendingLoad();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrAcquireChooseHistory) {
			toggleCommandHistoryList();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrAcquireAcceptHistory) {
			acceptCommandHistorySelection();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrAcquireExec) {
			executeAcquireCommand();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrAcquireLoad) {
			loadSelectedPaths(false);
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrAcquireLoadAll) {
			loadSelectedPaths(true);
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmHelp) {
			showAcquireHelp();
			clearEvent(event);
			return;
		}
		MRDialogFoundation::handleEvent(event);
	}

  private:
	void updateButtons() {
		const bool hasSelection = mListView != nullptr && !mListView->selectedValue().empty();
		const bool hasEntries = !resolvedPaths.empty();
		const bool enabled = !loadPending;

		if (buttonsRow.size() >= 2) {
			buttonsRow[0]->setState(sfDisabled, !enabled || !hasEntries);
			buttonsRow[1]->setState(sfDisabled, !enabled || !hasSelection);
		}
	}

	void persistSettings() {
		MRAcquireSettings settings = configuredAcquireSettings();
		char buffer[sizeof(commandBuffer)] = {0};
		std::string errorText;

		if (mCommandField != nullptr) mCommandField->getData(buffer);
		settings.commandLine = trimAscii(buffer);
		settings.commandHistory = commandHistory;
		static_cast<void>(setConfiguredAcquireSettings(settings, &errorText));
	}

	void rememberCommand(std::string commandLine) {
		commandLine = trimAscii(commandLine);
		if (commandLine.empty()) return;
		commandHistory.erase(std::remove(commandHistory.begin(), commandHistory.end(), commandLine), commandHistory.end());
		commandHistory.insert(commandHistory.begin(), commandLine);
		if (commandHistory.size() > 15) commandHistory.resize(15);
	}

	void refreshResolvedPaths(const std::string &output) {
		std::set<std::string> seen;
		std::size_t pos = 0;
		const MRDialogHistoryScope scope = mode == MRAcquireMode::LoadFile ? MRDialogHistoryScope::LoadFile : MRDialogHistoryScope::OpenFile;

		resolvedPaths.clear();
		while (pos <= output.size()) {
			const std::size_t end = output.find('\n', pos);
			const std::string_view line = end == std::string::npos ? std::string_view(output).substr(pos) : std::string_view(output).substr(pos, end - pos);
			const std::string candidate = decodeAcquirePathCandidate(line);
			std::string resolvedPath;

			if (!candidate.empty() && resolveReadableExistingPath(scope, candidate.c_str(), resolvedPath, false) && seen.insert(resolvedPath).second) resolvedPaths.push_back(resolvedPath);
			if (end == std::string::npos) break;
			pos = end + 1;
		}
		if (mListView != nullptr) mListView->updateItems();
		updateButtons();
	}

	void executeAcquireCommand() {
		char buffer[sizeof(commandBuffer)] = {0};
		std::string output;

		if (mCommandField != nullptr) mCommandField->getData(buffer);
		rememberCommand(buffer);
		persistSettings();
		if (!captureShellStdout(trimAscii(buffer), output)) output.clear();
		refreshResolvedPaths(output);
	}

	void loadSelectedPaths(bool loadAll) {
		std::vector<std::string> paths;

		if (loadAll) paths = resolvedPaths;
		else if (mListView != nullptr) {
			const std::string selected = mListView->selectedValue();
			if (!selected.empty()) paths.push_back(selected);
		}
		if (paths.empty()) return;
		persistSettings();
		pendingPaths = std::move(paths);
		loadPending = true;
		updateButtons();
	}

	void performPendingLoad() {
		std::vector<std::string> paths;

		if (!loadPending || pendingPaths.empty()) {
			loadPending = false;
			pendingPaths.clear();
			updateButtons();
			return;
		}
		paths.swap(pendingPaths);
		loadPending = false;
		updateButtons();
		if (mode == MRAcquireMode::LoadFile) static_cast<void>(loadResolvedFilesIntoWindows(paths, MRLoadedWindowActivation::KeepBackground, backgroundRestoreWindow));
		else
			static_cast<void>(openResolvedFilesIntoWindows(paths, MRLoadedWindowActivation::KeepBackground, backgroundRestoreWindow));
		reactivateAfterBackgroundLoad();
	}

	void reactivateAfterBackgroundLoad() {
		if (owner != nullptr) makeFirst();
		if (TProgram::deskTop != nullptr) TProgram::deskTop->setCurrent(this, TView::normalSelect);
		else
			select();
		if (mListView != nullptr) mListView->select();
	}

	void toggleCommandHistoryList() {
		const std::string currentValue = mCommandField != nullptr ? std::string(mCommandField->data) : std::string();

		if (commandHistory.empty()) return;
		commandHistoryDropList.toggle(*this, commandHistoryAnchor, commandHistory, currentValue, this, cmMrAcquireAcceptHistory, commandHistoryVisibleRows());
	}

	short commandHistoryVisibleRows() const {
		short visibleRows = 7;

		if (visibleRows > size.y - commandHistoryAnchor.a.y - 1) visibleRows = static_cast<short>(size.y - commandHistoryAnchor.a.y - 1);
		if (visibleRows < 1) visibleRows = 1;
		return visibleRows;
	}

	void acceptCommandHistorySelection() {
		std::string value;

		if (!commandHistoryDropList.acceptSelection(value) || mCommandField == nullptr) return;
		strnzcpy(mCommandField->data, value.c_str(), mCommandField->maxLen + 1);
		mCommandField->selectAll(True);
		mCommandField->drawView();
	}

	void showAcquireHelp() {
		messageBox(mfInformation | mfOKButton, "Exec runs the shell command hidden and collects readable file paths from stdout.\nLoad opens the selected file.\nLoad all opens all listed files.");
	}

	MRAcquireMode mode;
	char commandBuffer[256]{};
	TInputLine *mCommandField = nullptr;
	TScrollBar *mScrollBar = nullptr;
	AcquireListView *mListView = nullptr;
	std::vector<TButton *> buttonsRow;
	std::vector<std::string> commandHistory;
	std::vector<std::string> resolvedPaths;
	std::vector<std::string> pendingPaths;
	TRect commandHistoryAnchor;
	MRDropList commandHistoryDropList;
	MREditWindow *backgroundRestoreWindow = nullptr;
	bool loadPending = false;
};

} // namespace

ushort runAcquireDialog(MRAcquireMode mode) {
	TAcquireDialog *dialog = nullptr;
	MREditWindow *backgroundRestoreWindow = chooseAcquireBackgroundRestoreWindow();
	MREditWindow *topWindowAfterDismiss = nullptr;
	ushort result = cmCancel;

	if (TProgram::deskTop == nullptr) return cmCancel;
	dialog = new TAcquireDialog(mode, backgroundRestoreWindow);
	if (dialog == nullptr) return cmCancel;
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);

	topWindowAfterDismiss = chooseAcquireTopWindowAfterDismiss();
	if (canRestoreAcquireBackgroundWindow(topWindowAfterDismiss)) mrScheduleWindowActivation(topWindowAfterDismiss);
	else if (canRestoreAcquireBackgroundWindow(backgroundRestoreWindow))
		mrScheduleWindowActivation(backgroundRestoreWindow);
	else if (TProgram::application != nullptr)
		message(TProgram::application, evCommand, cmMrEnsureUsableWorkWindow, nullptr);
	else
		static_cast<void>(mrEnsureUsableWorkWindow(false));
	return result;
}
