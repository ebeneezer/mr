#define Uses_TApplication
#define Uses_TButton
#define Uses_TDialog
#define Uses_TDeskTop
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TListViewer
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TDrawBuffer
#define Uses_TFileDialog
#include <tvision/tv.h>

#include "MRWindowList.hpp"
#include "MRDirtyGating.hpp"
#include "setup/MRSetupCommon.hpp"
#include "../app/MRCommands.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

#include "../app/commands/MRWindowCommands.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRBentoBox/MRBentoBox.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/MRWindowLayout.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "../ui/MRDesktopWindow.hpp"
#include "../app/commands/MRFileCommands.hpp"

namespace {
TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

enum : ushort {
	cmMRWindowListDelete = 200,
	cmMRWindowListSave,
	cmMRWindowListHide,
	cmMRWindowListHideAll,
	cmMRWindowListSaveAll,
	cmMRWindowListSaveAs,
	cmMRWindowListRevert,
	cmMRWindowListGet,
	cmMRWorkspaceSave,
	cmMRWorkspaceLoad,
	cmMRWorkspaceAutosaveToggle,
	cmMRWorkspaceAutoloadToggle,
	cmMRWorkspaceMainFile,
	cmMRWindowListActivate,
};

class WindowListDialog;

WindowListDialog *g_manageWindowListDialog = nullptr;
constexpr const char *kHideToggleTitle = "Un/~H~ide";
constexpr const char *kHideAllTitle = "Hide ~a~ll";
constexpr const char *kRestoreTitle = "~R~estore";
constexpr const char *kRestoreAllTitle = "Restore ~a~ll";
constexpr const char *kGetTitle = "~G~et";
constexpr const char *kAutoWorkspaceOnTitle = "[x] Auto";
constexpr const char *kAutoWorkspaceOffTitle = "[ ] Auto";
constexpr const char *kMainWorkspaceOnTitle = "[x] ~M~ain";
constexpr const char *kMainWorkspaceOffTitle = "[ ] ~M~ain";

void updateButtonTitle(TButton *button, const char *title) {
	if (button == nullptr || title == nullptr || std::strcmp(button->title, title) == 0) return;
	delete[] (char *) button->title;
	button->title = newStr(title);
	button->drawView();
}

bool windowListDebugEnabled() noexcept {
	static int cached = -1;

	if (cached < 0) {
		const char *value = std::getenv("MR_KEY_DEBUG");
		cached = (value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0) ? 1 : 0;
	}
	return cached == 1;
}

void logWindowListTiming(const std::string &label, long long tookUs, const std::string &detail) {
	std::ostringstream line;

	line << label << " took_us=" << tookUs;
	if (!detail.empty()) line << " " << detail;
	mrLogMessage(line.str());
}

void postWindowListClose(TView *dialog) {
	if (dialog == nullptr) return;
	TEvent closeEvent{};
	closeEvent.what = evCommand;
	closeEvent.message.command = cmClose;
	closeEvent.message.infoPtr = dialog;
	dialog->putEvent(closeEvent);
}

void postWindowListActivate(TView *dialog) {
	if (dialog == nullptr) return;
	TEvent activateEvent{};
	activateEvent.what = evCommand;
	activateEvent.message.command = cmMRWindowListActivate;
	activateEvent.message.infoPtr = dialog;
	dialog->putEvent(activateEvent);
}

struct WindowListEntry {
	MREditWindow *window;
	MRDesktopWindow *desktopWindow;
	std::string statusLabel;
	std::string desktopLabel;
	std::string fileLabel;
	std::string directoryLabel;
	int virtualDesktop;
	bool hidden;
	bool minimized;

	WindowListEntry() : window(nullptr), desktopWindow(nullptr), virtualDesktop(1), hidden(false), minimized(false) {
	}
};

std::string currentWorkingDirectory() {
	char cwd[1024];
	if (::getcwd(cwd, sizeof(cwd)) == nullptr) return std::string();
	return std::string(cwd);
}

std::string trimCopy(const std::string &value) {
	std::string out = value;
	while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back())) != 0)
		out.pop_back();
	return out;
}

std::string baseNameOf(const std::string &path) {
	std::size_t pos = path.find_last_of("\\/");
	if (pos == std::string::npos) return path;
	return path.substr(pos + 1);
}

std::string directoryOf(const std::string &path) {
	std::size_t pos = path.find_last_of("\\/");
	if (pos == std::string::npos) return currentWorkingDirectory();
	if (pos == 2 && path[1] == ':') return path.substr(0, pos + 1);
	if (pos == 0) return path.substr(0, 1);
	return path.substr(0, pos);
}

std::string padRight(const std::string &value, std::size_t width) {
	if (value.size() >= width) return value.substr(0, width);
	return value + std::string(width - value.size(), ' ');
}

bool isWindowEmptyUntitled(MREditWindow *win) {
	if (win == nullptr) return false;
	if (win->currentFileName()[0] != '\0') return false;
	return win->isBufferEmpty();
}

std::vector<MREditWindow *> allEditWindows() {
	return allEditWindowsInZOrder();
}

bool containsWindow(const std::vector<MREditWindow *> &windows, MREditWindow *candidate) {
	return std::find(windows.begin(), windows.end(), candidate) != windows.end();
}

MREditWindow *preferredLinkTarget(MREditWindow *current) {
	std::vector<MREditWindow *> windows = allEditWindows();
	MREditWindow *firstOther = nullptr;
	MREditWindow *emptyUntitled = nullptr;
	MREditWindow *sameFile = nullptr;
	std::string currentFile;

	if (current == nullptr) return nullptr;
	currentFile = current->currentFileName();

	for (auto &window : windows) {
		if (window == current) continue;
		if (firstOther == nullptr) firstOther = window;
		if (emptyUntitled == nullptr && isWindowEmptyUntitled(window)) emptyUntitled = window;
		if (!currentFile.empty() && sameFile == nullptr && currentFile == window->currentFileName()) sameFile = window;
	}
	if (emptyUntitled != nullptr) return emptyUntitled;
	if (sameFile != nullptr) return sameFile;
	return firstOther;
}

bool isFileCompareWindowListCandidate(MREditWindow *window, MREditWindow *current) {
	if (window == nullptr || window == current || window->getEditor() == nullptr || window->hasTrackedExternalIoTasks()) return false;
	switch (window->windowRole()) {
		case MREditWindow::wrCommunicationCommand:
		case MREditWindow::wrCommunicationPipe:
		case MREditWindow::wrCommunicationDevice:
		case MREditWindow::wrLog:
		case MREditWindow::wrHelp:
			return false;
		default:
			break;
	}
	MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);
	return bentoBox == nullptr || bentoBox->allowsDocumentViewportSplit();
}

bool saveWindow(MREditWindow *win) {
	std::string logPath;
	std::string saveError;

	if (win == nullptr) return false;
	if (win->isReadOnly()) {
		if (win->windowRole() == MREditWindow::wrLog) {
			logPath = win->windowRoleDetail().empty() ? configuredLogFilePath() : win->windowRoleDetail();
			if (logPath.empty()) {
				mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "No log file URI configured.", mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
				mrLogMessage("Save rejected for log window without target path.");
				return false;
			}
			if (!mrAppendLogBufferToFile(logPath, &saveError)) {
				mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, saveError, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
				mrLogMessage("Save failed.");
				return false;
			}
			win->setWindowRole(MREditWindow::wrLog, logPath);
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Log window saved.", mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
			mrLogMessage("Log window saved.");
			return true;
		}
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Window is read-only.", mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
		mrLogMessage("Save rejected for read-only window.");
		return false;
	}
	if (!win->isFileChanged()) return true;
	if (win->canSaveInPlace()) {
		if (!win->saveCurrentFile()) {
			mrLogMessage("Save failed.");
			return false;
		}
		mrLogMessage("Window saved.");
		return true;
	}
	if (!win->saveCurrentFileAs()) {
		mrLogMessage("Save failed.");
		return false;
	}
	mrLogMessage("Window saved as a new file.");
	return true;
}

void closeWindow(MREditWindow *win) {
	if (win != nullptr) message(win, evCommand, cmClose, nullptr);
}

void hideWindow(MREditWindow *win) {
	if (win != nullptr) {
		setWindowManuallyHidden(win, true);
		win->hide();
	}
}

class WindowListView : public TListViewer {
  public:
	WindowListView(const TRect &bounds, TScrollBar *aVScrollBar, const std::vector<std::string> &aItems) noexcept : TListViewer(bounds, 1, nullptr, aVScrollBar), items(aItems) {
		eventMask |= evMouseWheel;
		setRange(static_cast<short>(items.size()));
	}

	void setItems(const std::vector<std::string> &aItems) {
		items = aItems;
		setRange(static_cast<short>(items.size()));
		if (items.empty()) focusItemNum(0);
		else if (focused >= range)
			focusItemNum(range - 1);
	}

	void getText(char *dest, short item, short maxLen) override {
		std::size_t copyLen;

		if (dest == nullptr || maxLen <= 0) return;
		if (item < 0 || static_cast<std::size_t>(item) >= items.size()) {
			dest[0] = EOS;
			return;
		}

		copyLen = static_cast<std::size_t>(maxLen - 1);
		std::strncpy(dest, items[item].c_str(), copyLen);
		dest[copyLen] = EOS;
	}

	void handleEvent(TEvent &event) override {
		const bool isDoubleClickActivation = event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0 && (event.mouse.eventFlags & meDoubleClick) != 0;
		TView *target = owner != nullptr && owner->owner != nullptr ? owner->owner : owner;
		int wheelDelta = 0;
		short nextFocused = 0;

		if (event.what == evMouseWheel && containsMouse(event) && range > 0) {
			if (event.mouse.wheel == mwUp || event.mouse.wheel == mwLeft) wheelDelta = -1;
			else if (event.mouse.wheel == mwDown || event.mouse.wheel == mwRight)
				wheelDelta = 1;
			if (wheelDelta != 0) {
				nextFocused = static_cast<short>(std::clamp<int>(focused + wheelDelta, 0, range - 1));
				focusItemNum(nextFocused);
				clearEvent(event);
				return;
			}
		}

		TListViewer::handleEvent(event);
		if (isDoubleClickActivation && focused >= 0 && focused < range && target != nullptr) {
			message(target, evCommand, cmOK, nullptr);
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter && focused >= 0 && focused < range && target != nullptr) {
			message(target, evCommand, cmOK, nullptr);
			clearEvent(event);
		}
	}

  private:
	std::vector<std::string> items;
};

class WindowListCounterView : public TView {
  public:
	explicit WindowListCounterView(const TRect &bounds) : TView(bounds), text("0|0") {
	}

	void setCounter(int focusedIndex, std::size_t total) {
		std::string next;

		if (total == 0 || focusedIndex < 0) next = "0|0";
		else
			next = std::to_string(focusedIndex + 1) + "|" + std::to_string(total);
		if (next == text) return;
		text = next;
		drawView();
	}

	void draw() override {
		TDrawBuffer buffer;
		TColorAttr color = getColor(1);
		std::string shown = text;
		int start = size.x - static_cast<int>(shown.size());

		buffer.moveChar(0, ' ', color, size.x);
		if (start < 0) {
			shown = shown.substr(shown.size() - static_cast<std::size_t>(size.x));
			start = 0;
		}
		if (!shown.empty()) buffer.moveStr(static_cast<ushort>(start), shown.c_str(), color, std::max(0, size.x - start));
		writeLine(0, 0, size.x, 1, buffer);
	}

  private:
	std::string text;
};

class WindowListDialog : public MRDialogFoundation {
  public:
	~WindowListDialog() override {
		if (g_manageWindowListDialog == this) g_manageWindowListDialog = nullptr;
	}

	void saveWorkspaceWithDialog() {
		char fileName[MAXPATH];

		mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::WorkspaceSave, fileName, sizeof(fileName), "*.mrmac");
		if (mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::WorkspaceSave, "*.mrmac", "SAVE WORKSPACE AS", "~N~ame", fdOKButton | fdReplaceButton, fileName) != cmCancel) {
			std::string name(fileName);
			if (name.find(".mrmac") == std::string::npos) name += ".mrmac";
			if (::access(name.c_str(), F_OK) == 0 && mr::dialogs::showUnsavedChangesDialog("Overwrite", "Workspace file exists. Overwrite?", name.c_str()) != mr::dialogs::UnsavedChangesChoice::Save) return;
			mrSaveWorkspace(name);
			rememberLoadDialogPath(MRDialogHistoryScope::WorkspaceSave, name.c_str());
			rememberLoadDialogPath(MRDialogHistoryScope::WorkspaceLoad, name.c_str());
		}
	}

	void loadWorkspaceWithDialog() {
		bool detached = false;
		bool loaded = false;

		if (mode == mrwlManageWindows && g_manageWindowListDialog == this) {
			g_manageWindowListDialog = nullptr;
			detached = true;
		}
		loaded = mrLoadWorkspaceWithDialog();
		if (loaded && mode == mrwlManageWindows) postWindowListClose(this);
		else if (detached && g_manageWindowListDialog == nullptr)
			g_manageWindowListDialog = this;
	}

	bool persistWorkspaceToggleSettings() {
		std::string errorText;

		if (persistConfiguredSettingsSnapshot(&errorText)) return true;
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorText.empty() ? "Unable to save workspace settings." : errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return false;
	}

	void updateWorkspaceToggleButtons() {
		updateButtonTitle(autosaveWorkspaceButton, configuredAutosaveWorkspace() ? kAutoWorkspaceOnTitle : kAutoWorkspaceOffTitle);
		updateButtonTitle(autoloadWorkspaceButton, configuredAutoloadWorkspace() ? kAutoWorkspaceOnTitle : kAutoWorkspaceOffTitle);
	}

	void toggleAutosaveWorkspace() {
		std::string errorText;
		const bool enabled = !configuredAutosaveWorkspace();

		if (!setConfiguredAutosaveWorkspace(enabled, &errorText)) {
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorText.empty() ? "Unable to change auto save workspace." : errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return;
		}
		if (enabled) setRuntimePreserveAutosavedWorkspace(false);
		if (persistWorkspaceToggleSettings()) {
			if (!enabled) static_cast<void>(mrClearAutosavedWorkspace());
			updateWorkspaceToggleButtons();
		}
	}

	void toggleAutoloadWorkspace() {
		std::string errorText;
		const bool enabled = !configuredAutoloadWorkspace();

		if (!setConfiguredAutoloadWorkspace(enabled, &errorText)) {
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorText.empty() ? "Unable to change auto load workspace." : errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return;
		}
		if (persistWorkspaceToggleSettings()) updateWorkspaceToggleButtons();
	}

	WindowListDialog(MRWindowListMode aMode, MREditWindow *aCurrent, MREditWindow *aPreferred) : TWindowInit(initMrDialogFrame), MRDialogFoundation(centeredSetupDialogRect(computeWidth(), computeHeight(aMode, aCurrent)), "WINDOW LIST", computeWidth(), computeHeight(aMode, aCurrent), initMrDialogFrame), mode(aMode), current(aCurrent), preferred(aPreferred), listView(nullptr), scrollBar(nullptr), hideToggleButton(nullptr), hideAllButton(nullptr), getButton(nullptr), workspaceMainFileButton(nullptr), autosaveWorkspaceButton(nullptr), autoloadWorkspaceButton(nullptr), counterView(nullptr), selected(nullptr), lastFocusedIndex(-1) {
		int width = computeWidth();
		int height = computeHeight(aMode, aCurrent);
		int listTop = 7;
		int listBottom = height - 6;
		const int topButtonY = 2;
		const int actionButtonY = 4;
		const int workspaceButtonY = height - 5;
		const int bottomButtonY = height - 3;
		const int buttonGap = 2;
		const int workspacePairGap = 1;
		const int workspaceGroupGap = 6;
		int topButtonLeft = 2;
		int topButtonWidth = 0;
		auto centeredRowStart = [width](int contentWidth) { return std::max(2, (width - contentWidth) / 2); };

		{
			const std::array topButtons{mr::dialogs::DialogButtonSpec{"~D~elete", cmMRWindowListDelete, bfNormal}, mr::dialogs::DialogButtonSpec{"~S~ave", cmMRWindowListSave, bfNormal}, mr::dialogs::DialogButtonSpec{kHideToggleTitle, cmMRWindowListHide, bfNormal}, mr::dialogs::DialogButtonSpec{kHideAllTitle, cmMRWindowListHideAll, bfNormal}};
			const std::array widthCandidates{kHideToggleTitle, kHideAllTitle, kRestoreTitle, kRestoreAllTitle, "Save ~a~ll", "Save a~s~", "~R~evert", kGetTitle, kMainWorkspaceOnTitle};
			std::vector<TButton *> topButtonViews;
			int minTopButtonWidth = 0;
			mr::dialogs::DialogButtonRowMetrics metrics;

			for (const char *title : widthCandidates) {
				const std::array candidate{mr::dialogs::DialogButtonSpec{title, 0, bfNormal}};
				minTopButtonWidth = std::max(minTopButtonWidth, mr::dialogs::measureUniformButtonRow(candidate, buttonGap).buttonWidth);
			}
			metrics = mr::dialogs::measureUniformButtonRow(topButtons, buttonGap, minTopButtonWidth);
			const int left = centeredRowStart(metrics.rowWidth);
			topButtonLeft = left;
			topButtonWidth = minTopButtonWidth;

			mr::dialogs::insertUniformButtonRow(*this, left, topButtonY, buttonGap, topButtons, minTopButtonWidth, &topButtonViews);
			if (topButtonViews.size() >= 3) hideToggleButton = topButtonViews[2];
			if (topButtonViews.size() >= 4) hideAllButton = topButtonViews[3];
		}
		{
			const std::array actionButtons{mr::dialogs::DialogButtonSpec{"Save ~a~ll", cmMRWindowListSaveAll, bfNormal}, mr::dialogs::DialogButtonSpec{"Save a~s~", cmMRWindowListSaveAs, bfNormal}, mr::dialogs::DialogButtonSpec{"~R~evert", cmMRWindowListRevert, bfNormal}, mr::dialogs::DialogButtonSpec{kMainWorkspaceOffTitle, cmMRWorkspaceMainFile, bfNormal}};
			std::vector<TButton *> actionButtonViews;

			mr::dialogs::insertUniformButtonRow(*this, topButtonLeft, actionButtonY, buttonGap, actionButtons, topButtonWidth, &actionButtonViews);
			if (actionButtonViews.size() >= 4) {
				workspaceMainFileButton = actionButtonViews[3];
				getButton = new TButton(TRect(workspaceMainFileButton->origin.x, workspaceMainFileButton->origin.y, workspaceMainFileButton->origin.x + workspaceMainFileButton->size.x, workspaceMainFileButton->origin.y + workspaceMainFileButton->size.y), kGetTitle, cmMRWindowListGet, bfNormal);
				insert(getButton);
				getButton->hide();
			}
		}

		scrollBar = new TScrollBar(TRect(width - 3, listTop, width - 2, listBottom));
		insert(scrollBar);
		listView = new WindowListView(TRect(2, listTop, width - 3, listBottom), scrollBar, std::vector<std::string>());
		insert(listView);

		{
			const std::array autoSaveCandidate{mr::dialogs::DialogButtonSpec{kAutoWorkspaceOnTitle, 0, bfNormal}, mr::dialogs::DialogButtonSpec{kAutoWorkspaceOffTitle, 0, bfNormal}};
			const std::array saveCandidate{mr::dialogs::DialogButtonSpec{"Sa~v~e workspace", 0, bfNormal}};
			const std::array loadCandidate{mr::dialogs::DialogButtonSpec{"~L~oad workspace", 0, bfNormal}};
			const int autoWidth = mr::dialogs::measureUniformButtonRow(autoSaveCandidate, 0).buttonWidth;
			const int saveWidth = mr::dialogs::measureUniformButtonRow(saveCandidate, 0).buttonWidth;
			const int loadWidth = mr::dialogs::measureUniformButtonRow(loadCandidate, 0).buttonWidth;
			const int rowWidth = autoWidth + workspacePairGap + saveWidth + workspaceGroupGap + autoWidth + workspacePairGap + loadWidth;
			int left = centeredRowStart(rowWidth);

			autosaveWorkspaceButton = new TButton(TRect(left, workspaceButtonY, left + autoWidth, workspaceButtonY + 2), configuredAutosaveWorkspace() ? kAutoWorkspaceOnTitle : kAutoWorkspaceOffTitle, cmMRWorkspaceAutosaveToggle, bfNormal);
			insert(autosaveWorkspaceButton);
			left += autoWidth + workspacePairGap;
			insert(new TButton(TRect(left, workspaceButtonY, left + saveWidth, workspaceButtonY + 2), "Sa~v~e workspace", cmMRWorkspaceSave, bfNormal));
			left += saveWidth + workspaceGroupGap;
			autoloadWorkspaceButton = new TButton(TRect(left, workspaceButtonY, left + autoWidth, workspaceButtonY + 2), configuredAutoloadWorkspace() ? kAutoWorkspaceOnTitle : kAutoWorkspaceOffTitle, cmMRWorkspaceAutoloadToggle, bfNormal);
			insert(autoloadWorkspaceButton);
			left += autoWidth + workspacePairGap;
			insert(new TButton(TRect(left, workspaceButtonY, left + loadWidth, workspaceButtonY + 2), "~L~oad workspace", cmMRWorkspaceLoad, bfNormal));
		}
		{
			if (mode == mrwlManageWindows) {
				const std::array bottomButtons{mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
				const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(bottomButtons, buttonGap);
				const int left = centeredRowStart(metrics.rowWidth);

				mr::dialogs::insertUniformButtonRow(*this, left, bottomButtonY, buttonGap, bottomButtons);
			} else {
				const std::array bottomButtons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
				const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(bottomButtons, buttonGap);
				const int left = centeredRowStart(metrics.rowWidth);

				mr::dialogs::insertUniformButtonRow(*this, left, bottomButtonY, buttonGap, bottomButtons);
			}
		}
		counterView = new WindowListCounterView(TRect(width - 12, height - 2, width - 2, height - 1));
		insert(counterView);

		refreshEntries();
		focusPreferred();
		listView->select();
		updateHideToggleState();
		setDialogValidationHook([this]() {
			DialogValidationResult result;
			result.valid = mode == mrwlManageWindows ? currentEntry() != nullptr : currentSelection() != nullptr;
			if (!result.valid) result.warningText = "Select window.";
			return result;
		});
	}

	MREditWindow *selectedWindow() const {
		if (selected != nullptr) return selected;
		return currentSelection();
	}

	void activateModeless() {
		std::string line;
		refreshEntries();
		updateWorkspaceToggleButtons();
		focusPreferred();
		if (windowListDebugEnabled()) {
			line = "Window List activateModeless before visible=";
			line += (state & sfVisible) != 0 ? "1" : "0";
			line += " selected=";
			line += (state & sfSelected) != 0 ? "1" : "0";
			line += " focus=";
			line += listView != nullptr ? std::to_string(listView->focused) : "-1";
			mrLogMessage(line);
		}
		if (owner != nullptr) makeFirst();
		if (TProgram::deskTop != nullptr) TProgram::deskTop->setCurrent(this, TView::normalSelect);
		else
			select();
		if (listView != nullptr) listView->select();
		if (windowListDebugEnabled()) {
			line = "Window List activateModeless after visible=";
			line += (state & sfVisible) != 0 ? "1" : "0";
			line += " selected=";
			line += (state & sfSelected) != 0 ? "1" : "0";
			line += " focus=";
			line += listView != nullptr ? std::to_string(listView->focused) : "-1";
			mrLogMessage(line);
		}
	}

	void returnDialogToFront() {
		if (owner != nullptr) makeFirst();
		if (TProgram::deskTop != nullptr) TProgram::deskTop->setCurrent(this, TView::normalSelect);
		else
			select();
		if (listView != nullptr) listView->select();
	}

	void previewFocusedWindow() {
		WindowListEntry *entry = currentEntry();

		if (mode != mrwlManageWindows) return;
		if (entry == nullptr || entry->desktopWindow == nullptr || entry->desktopWindow->desktopNativeWindow() == nullptr) return;
		if (entry->minimized || entry->hidden || entry->virtualDesktop != currentVirtualDesktop()) return;
		if ((entry->desktopWindow->desktopNativeWindow()->state & sfVisible) == 0) return;
		entry->desktopWindow->desktopNativeWindow()->select();
		returnDialogToFront();
	}

	void closeModelessWindowList() {
		if (mode == mrwlManageWindows && g_manageWindowListDialog == this) g_manageWindowListDialog = nullptr;
		close();
	}

	void handleEvent(TEvent &event) override {
		const auto eventStartedAt = std::chrono::steady_clock::now();
		const ushort originalWhat = event.what;
		const ushort originalCommand = event.what == evCommand || event.what == evBroadcast ? event.message.command : 0;
		const int oldFocusForTiming = listView != nullptr ? listView->focused : -1;

		if (event.what == evCommand && event.message.command == cmMRWindowListActivate && event.message.infoPtr == this) {
			activateModeless();
			{
				const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - eventStartedAt).count();
				logWindowListTiming("Window List activate event timing", tookUs, "entries=" + std::to_string(entries.size()));
			}
			clearEvent(event);
			return;
		}
		if (mode == mrwlManageWindows && event.what == evCommand && event.message.command == cmClose && (event.message.infoPtr == nullptr || event.message.infoPtr == this)) {
			clearEvent(event);
			closeModelessWindowList();
			return;
		}
		if (event.what == evBroadcast && event.message.command == cmMrWindowTopologyChanged) {
			if (mode == mrwlManageWindows) {
				const auto refreshStartedAt = std::chrono::steady_clock::now();
				refreshEntries();
				const long long refreshUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - refreshStartedAt).count();
				long long drawUs = 0;
				const auto drawStartedAt = std::chrono::steady_clock::now();
				if (listView != nullptr) listView->drawView();
				drawView();
				drawUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - drawStartedAt).count();
				if ((state & sfVisible) != 0 && TProgram::deskTop != nullptr) {
					MREditWindow *currentWindow = dynamic_cast<MREditWindow *>(TProgram::deskTop->current);
					if (currentWindow != nullptr && currentWindow->isMinimized()) activateModeless();
				}
				{
					std::ostringstream detail;
					const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - eventStartedAt).count();

					detail << "entries=" << entries.size() << " refresh_us=" << refreshUs << " draw_us=" << drawUs;
					logWindowListTiming("Window List topology timing", tookUs, detail.str());
				}
			}
			clearEvent(event);
			return;
		}
		if (mode == mrwlManageWindows && event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEsc) {
			clearEvent(event);
			closeModelessWindowList();
			return;
		}
		if (mode == mrwlManageWindows && event.what == evCommand && event.message.command == cmCancel) {
			clearEvent(event);
			closeModelessWindowList();
			return;
		}
		if (event.what == evCommand && event.message.command == cmOK) {
			selected = currentSelection();
			if (mode != mrwlManageWindows && selected == nullptr) {
				clearEvent(event);
				return;
			}
			if (mode == mrwlManageWindows) {
				WindowListEntry *entry = currentEntry();

				if (entry == nullptr || entry->desktopWindow == nullptr || entry->desktopWindow->desktopNativeWindow() == nullptr) {
					clearEvent(event);
					return;
				}
				if (entry->desktopWindow->desktopMinimized()) entry->desktopWindow->restoreDesktopWindow();
				entry->desktopWindow->desktopNativeWindow()->select();
				clearEvent(event);
				closeModelessWindowList();
				return;
			}
		}

		{
			const auto phaseStartedAt = std::chrono::steady_clock::now();
			MRDialogFoundation::handleEvent(event);
			const long long phaseUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
			if (phaseUs >= 10000) logWindowListTiming("Window List base event slow", phaseUs, "what=" + std::to_string(originalWhat) + " command=" + std::to_string(originalCommand));
		}

		if (listView != nullptr && listView->focused != lastFocusedIndex) {
			const auto phaseStartedAt = std::chrono::steady_clock::now();
			updateHideToggleState();
			previewFocusedWindow();
			{
				std::ostringstream detail;
				const long long phaseUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();

				detail << "old_focus=" << oldFocusForTiming << " new_focus=" << listView->focused << " entries=" << entries.size();
				logWindowListTiming("Window List focus timing", phaseUs, detail.str());
			}
		}

		if (event.what == evKeyDown) {
			switch (ctrlToArrow(event.keyDown.keyCode)) {
				case kbDel:
					handleDelete();
					clearEvent(event);
					return;
				case kbF3:
					handleSave();
					clearEvent(event);
					return;
				case kbF8:
					handleSaveAll();
					clearEvent(event);
					return;
				case kbF4:
					if (canToggleCurrentSelection()) handleHide();
					clearEvent(event);
					return;
				case kbF5:
					handleHideAll();
					clearEvent(event);
					return;
				case kbF6:
					saveWorkspaceWithDialog();
					clearEvent(event);
					return;
				case kbF7:
					loadWorkspaceWithDialog();
					clearEvent(event);
					return;
				case kbF1:
					static_cast<void>(mrShowProjectHelp());
					clearEvent(event);
					return;
			}
		}

		if (event.what != evCommand) return;

		switch (event.message.command) {
			case cmMRWindowListDelete:
				handleDelete();
				clearEvent(event);
				break;
			case cmMRWindowListSave:
				handleSave();
				clearEvent(event);
				break;
			case cmMRWindowListSaveAll:
				handleSaveAll();
				clearEvent(event);
				break;
			case cmMRWindowListSaveAs:
				handleSaveAs();
				clearEvent(event);
				break;
			case cmMRWindowListRevert:
				handleRevert();
				clearEvent(event);
				break;
			case cmMRWindowListHide:
				if (canToggleCurrentSelection()) handleHide();
				clearEvent(event);
				break;
			case cmMRWindowListHideAll:
				handleHideAll();
				clearEvent(event);
				break;
			case cmMRWindowListGet: {
				WindowListEntry *entry = currentEntry();
				if (entry != nullptr && entry->virtualDesktop != currentVirtualDesktop()) {
					TGroup *content = managedContent();
					if (entry->desktopWindow != nullptr) entry->desktopWindow->setDesktopIndex(currentVirtualDesktop());
					syncVirtualDesktopVisibility();
					MRWindowLayout::handleDesktopLayoutChange();
					mrNotifyWindowTopologyChanged();
					refreshEntries();
					updateHideToggleState();
					if (content != nullptr) content->drawView();
					if (listView != nullptr) listView->drawView();
					drawView();
				}
				clearEvent(event);
				break;
			}
			case cmMRWorkspaceSave:
				saveWorkspaceWithDialog();
				clearEvent(event);
				break;
			case cmMRWorkspaceLoad:
				loadWorkspaceWithDialog();
				clearEvent(event);
				break;
			case cmMRWorkspaceAutosaveToggle:
				toggleAutosaveWorkspace();
				clearEvent(event);
				break;
			case cmMRWorkspaceAutoloadToggle:
				toggleAutoloadWorkspace();
				clearEvent(event);
				break;
			case cmMRWorkspaceMainFile: {
				MREditWindow *win = currentSelection();
				if (mrIsWorkspaceMainFile(win)) {
					mrClearWorkspaceMainFile();
					refreshEntries();
					if (listView != nullptr) listView->drawView();
					drawView();
				} else if (mrSetWorkspaceMainFile(win)) {
					refreshEntries();
					if (listView != nullptr) listView->drawView();
					drawView();
				}
				clearEvent(event);
				break;
			}
			case cmHelp:
				static_cast<void>(mrShowProjectHelp());
				clearEvent(event);
				break;
		}
	}

  private:
	static int computeWidth() {
		TRect desk = TProgram::deskTop->getExtent();
		int deskWidth = desk.b.x - desk.a.x;
		return std::max(68, std::min(72, deskWidth - 2));
	}

	static int computeHeight(MRWindowListMode, MREditWindow *) {
		TRect desk = TProgram::deskTop->getExtent();
		int deskHeight = desk.b.y - desk.a.y;
		int listHeight = std::max(1, deskHeight / 2);
		return std::max(12, listHeight + 12);
	}

	std::string renderRow(const WindowListEntry &entry) const {
		std::string filePart = entry.fileLabel;
		std::string dirPart = trimCopy(entry.directoryLabel);
		return padRight(entry.statusLabel, 5) + " " + padRight(entry.desktopLabel, 2) + " " + (entry.window != nullptr && mrIsWorkspaceMainFile(entry.window) ? "♔ " : "  ") + padRight(filePart, 26) + " " + dirPart;
	}

	void collectEntries() {
		const auto startedAt = std::chrono::steady_clock::now();
		long long enumerateUs = 0;
		long long rowUs = 0;
		const auto enumerateStartedAt = startedAt;
		const std::vector<MREditWindow *> windows = allEditWindows();
		enumerateUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - enumerateStartedAt).count();
		entries.clear();
		rows.clear();
		{
			const auto phaseStartedAt = std::chrono::steady_clock::now();
			for (std::size_t i = 0; i < windows.size(); ++i) {
				WindowListEntry entry;
				std::string fileName = windows[i]->currentFileName();
				const char *title = windows[i]->getTitle(0);
				std::string titleText = title != nullptr ? title : "";

				if (mode == mrwlSelectLinkTarget && windows[i] == current) continue;
				if (mode == mrwlSelectFileCompareTarget && !isFileCompareWindowListCandidate(windows[i], current)) continue;

				entry.window = windows[i];
				entry.desktopWindow = windows[i];
				entry.hidden = isWindowManuallyHidden(windows[i]);
				entry.minimized = windows[i]->isMinimized();
				entry.statusLabel = entry.minimized ? "[min]" : (entry.hidden ? "[hid]" : "");
				entry.virtualDesktop = windows[i]->mVirtualDesktop;
				entry.desktopLabel = std::to_string(entry.virtualDesktop);
				entry.fileLabel = fileName.empty() ? (titleText.empty() ? "?No-File" : baseNameOf(titleText)) : baseNameOf(fileName);
				entry.directoryLabel = directoryOf(fileName.empty() ? currentWorkingDirectory() : fileName);
				entries.push_back(entry);
				rows.push_back(renderRow(entry));
			}
			if (mode == mrwlManageWindows) {
				const std::vector<MRDesktopWindow *> desktopWindows = allDesktopWindowsInZOrder();

				for (std::size_t index = 0; index < desktopWindows.size(); ++index) {
					MRDesktopWindow *desktopWindow = desktopWindows[index];
					WindowListEntry entry;
					TWindow *nativeWindow = desktopWindow != nullptr ? desktopWindow->desktopNativeWindow() : nullptr;
					MREditWindow *editWindow = dynamic_cast<MREditWindow *>(desktopWindow);

					if (nativeWindow == nullptr || editWindow != nullptr) continue;
					entry.desktopWindow = desktopWindow;
					entry.hidden = desktopWindow->desktopManuallyHidden();
					entry.virtualDesktop = desktopWindow->desktopIndex();
					entry.desktopLabel = std::to_string(entry.virtualDesktop);
					entry.statusLabel = entry.hidden ? "[hid]" : "";
					entry.fileLabel = nativeWindow->getTitle(0) != nullptr ? nativeWindow->getTitle(0) : "MRMac";
					entry.directoryLabel = "Desktop";
					entries.push_back(entry);
					rows.push_back(renderRow(entry));
				}
			}
			rowUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
		}
		{
			std::ostringstream detail;
			const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();

			detail << "windows=" << windows.size() << " entries=" << entries.size() << " enumerate_us=" << enumerateUs << " row_us=" << rowUs;
			if (tookUs >= 10000 || windows.size() >= 50) logWindowListTiming("Window List collect timing", tookUs, detail.str());
		}
	}

	void sanitizeTrackedWindows() {
		const auto startedAt = std::chrono::steady_clock::now();
		const std::vector<MREditWindow *> windows = allEditWindows();
		if (!containsWindow(windows, current)) current = nullptr;
		if (!containsWindow(windows, preferred)) preferred = nullptr;
		if (!containsWindow(windows, selected)) selected = nullptr;
		{
			const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
			if (tookUs >= 10000 || windows.size() >= 50) logWindowListTiming("Window List sanitize timing", tookUs, "windows=" + std::to_string(windows.size()));
		}
	}

	void refreshEntries() {
		const auto startedAt = std::chrono::steady_clock::now();
		int oldFocus = listView != nullptr ? listView->focused : 0;
		long long sanitizeUs = 0;
		long long collectUs = 0;
		long long setItemsUs = 0;
		long long focusUs = 0;
		long long buttonsUs = 0;
		{
			const auto phaseStartedAt = std::chrono::steady_clock::now();
			sanitizeTrackedWindows();
			sanitizeUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
		}
		{
			const auto phaseStartedAt = std::chrono::steady_clock::now();
			collectEntries();
			collectUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
		}
		if (listView != nullptr) {
			const auto phaseStartedAt = std::chrono::steady_clock::now();
			listView->setItems(rows);
			setItemsUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
		}
		if (entries.empty()) {
			selected = nullptr;
			if (mode == mrwlManageWindows) postWindowListClose(this);
			else
				endModal(cmCancel);
			return;
		}
		if (oldFocus < 0) oldFocus = 0;
		if (oldFocus >= static_cast<int>(entries.size())) oldFocus = static_cast<int>(entries.size()) - 1;
		{
			const auto phaseStartedAt = std::chrono::steady_clock::now();
			listView->focusItemNum(static_cast<short>(oldFocus));
			focusUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
		}
		{
			const auto phaseStartedAt = std::chrono::steady_clock::now();
			updateHideToggleState();
			buttonsUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
		}
		{
			std::ostringstream detail;
			const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();

			detail << "entries=" << entries.size() << " old_focus=" << oldFocus << " sanitize_us=" << sanitizeUs << " collect_us=" << collectUs << " set_items_us=" << setItemsUs << " focus_us=" << focusUs << " buttons_us=" << buttonsUs;
			if (tookUs >= 10000 || entries.size() >= 50) logWindowListTiming("Window List refresh timing", tookUs, detail.str());
		}
	}

	void focusPreferred() {
		int index = 0;
		MREditWindow *target = mode == mrwlActivateWindow ? current : preferred;
		for (std::size_t i = 0; i < entries.size(); ++i) {
			if (entries[i].window == target) {
				index = static_cast<int>(i);
				break;
			}
		}
		if (!entries.empty()) listView->focusItemNum(static_cast<short>(index));
	}

	MREditWindow *currentSelection() const {
		if (listView == nullptr || listView->focused < 0 || static_cast<std::size_t>(listView->focused) >= entries.size()) return nullptr;
		return entries[static_cast<std::size_t>(listView->focused)].window;
	}

	WindowListEntry *currentEntry() {
		if (listView == nullptr || listView->focused < 0 || static_cast<std::size_t>(listView->focused) >= entries.size()) return nullptr;
		return &entries[static_cast<std::size_t>(listView->focused)];
	}

	void handleDelete() {
		WindowListEntry *entry = currentEntry();

		if (entry == nullptr) return;
		if (entry->window != nullptr) {
			closeWindow(entry->window);
			static_cast<void>(mrEnsureUsableWorkWindow(false));
		} else if (entry->desktopWindow != nullptr && entry->desktopWindow->desktopNativeWindow() != nullptr)
			message(entry->desktopWindow->desktopNativeWindow(), evCommand, cmClose, entry->desktopWindow->desktopNativeWindow());
		refreshEntries();
	}

	void handleSave() {
		MREditWindow *win = currentSelection();
		if (win == nullptr) return;
		saveWindow(win);
		refreshEntries();
	}

	void handleSaveAll() {
		static_cast<void>(saveAllDirtyEditWindows());
		refreshEntries();
		activateModeless();
	}

	void handleSaveAs() {
		MREditWindow *win = currentSelection();
		if (win == nullptr) return;
		static_cast<void>(saveEditWindowAs(win));
		refreshEntries();
		activateModeless();
	}

	void handleRevert() {
		MREditWindow *win = currentSelection();
		if (win == nullptr) return;
		static_cast<void>(revertEditWindow(win));
		refreshEntries();
		activateModeless();
	}

	void handleHide() {
		WindowListEntry *entry = currentEntry();
		std::string line;
		if (entry == nullptr) return;
		if (entry->window != nullptr && entry->window->isMinimized()) {
			entry->window->restoreWindow();
			refreshEntries();
			return;
		}
		if (entry->window == nullptr) {
			if (entry->desktopWindow == nullptr) return;
			entry->desktopWindow->setDesktopManuallyHidden(!entry->hidden);
			syncVirtualDesktopVisibility();
			refreshEntries();
			return;
		}
		MREditWindow *win = entry->window;
		line = "Window List handleHide";
		line += (win->state & sfVisible) != 0 ? " hide " : " unhide ";
		line += "'";
		line += win->getTitle(0) != nullptr ? win->getTitle(0) : "?";
		line += "' visible=";
		line += (win->state & sfVisible) != 0 ? "1" : "0";
		line += " selected=";
		line += (win->state & sfSelected) != 0 ? "1" : "0";
		line += " hidden=";
		line += isWindowManuallyHidden(win) ? "1" : "0";
		mrLogMessage(line);
		if (!isWindowManuallyHidden(win)) {
			hideWindow(win);
			static_cast<void>(mrEnsureUsableWorkWindow());
		} else {
			setWindowManuallyHidden(win, false);
			selected = win;
			mrScheduleWindowActivation(win);
		}
		refreshEntries();
		if (windowListDebugEnabled()) {
			line = "Window List after toggle visible=";
			line += (state & sfVisible) != 0 ? "1" : "0";
			line += " selected=";
			line += (state & sfSelected) != 0 ? "1" : "0";
			line += " focus=";
			line += listView != nullptr ? std::to_string(listView->focused) : "-1";
			mrLogMessage(line);
		}
	}

	bool canToggleCurrentSelection() const {
		return const_cast<WindowListDialog *>(this)->currentEntry() != nullptr;
	}

	void updateHideToggleState() {
		const auto startedAt = std::chrono::steady_clock::now();
		WindowListEntry *entry = currentEntry();
		MREditWindow *win = entry != nullptr ? entry->window : nullptr;
		const bool enabled = canToggleCurrentSelection();
		const bool canGet = entry != nullptr && entry->virtualDesktop != currentVirtualDesktop();
		if (hideToggleButton != nullptr) hideToggleButton->setState(sfDisabled, enabled ? False : True);
		if (hideToggleButton != nullptr && entry != nullptr) {
			const char *title = win != nullptr && win->isMinimized() ? kRestoreTitle : kHideToggleTitle;
			if (std::strcmp(hideToggleButton->title, title) != 0) {
				delete[] (char *) hideToggleButton->title;
				hideToggleButton->title = newStr(title);
				hideToggleButton->drawView();
			}
		}
		if (hideAllButton != nullptr) {
			const char *title = win != nullptr && win->isMinimized() ? kRestoreAllTitle : kHideAllTitle;
			if (std::strcmp(hideAllButton->title, title) != 0) {
				delete[] (char *) hideAllButton->title;
				hideAllButton->title = newStr(title);
				hideAllButton->drawView();
			}
		}
		if (getButton != nullptr) {
			TGroup *content = managedContent();
			const bool wasVisible = (getButton->state & sfVisible) != 0;
			getButton->setState(sfDisabled, canGet ? False : True);
			if (canGet) {
				if (workspaceMainFileButton != nullptr) workspaceMainFileButton->hide();
				if (!wasVisible) {
					getButton->show();
					if (content != nullptr) content->drawView();
					drawView();
				}
			} else if (wasVisible) {
				getButton->hide();
				if (workspaceMainFileButton != nullptr) workspaceMainFileButton->show();
				if (content != nullptr) content->drawView();
				drawView();
			} else if (workspaceMainFileButton != nullptr && (workspaceMainFileButton->state & sfVisible) == 0) {
				workspaceMainFileButton->show();
				if (content != nullptr) content->drawView();
				drawView();
			}
		}
		if (workspaceMainFileButton != nullptr) {
			updateButtonTitle(workspaceMainFileButton, win != nullptr && mrIsWorkspaceMainFile(win) ? kMainWorkspaceOnTitle : kMainWorkspaceOffTitle);
			workspaceMainFileButton->setState(sfDisabled, win != nullptr ? False : True);
		}
		if (counterView != nullptr) counterView->setCounter(listView != nullptr ? listView->focused : -1, entries.size());
		lastFocusedIndex = listView != nullptr ? listView->focused : -1;
		{
			const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
			if (tookUs >= 5000) logWindowListTiming("Window List button state slow", tookUs, "focus=" + std::to_string(lastFocusedIndex));
		}
	}

	void handleHideAll() {
		std::vector<MREditWindow *> windows = allEditWindows();
		std::vector<MRDesktopWindow *> desktopWindows = allDesktopWindowsInZOrder();
		WindowListEntry *entry = currentEntry();

		if (entry != nullptr && entry->window != nullptr && entry->window->isMinimized()) {
			for (auto &window : windows)
				if (window != nullptr && window->isMinimized()) window->restoreWindow();
		} else {
			for (auto &window : windows)
				hideWindow(window);
			for (std::size_t index = 0; index < desktopWindows.size(); ++index)
				if (desktopWindows[index] != nullptr) desktopWindows[index]->setDesktopManuallyHidden(true);
			syncVirtualDesktopVisibility();
			static_cast<void>(mrEnsureUsableWorkWindow());
		}
		refreshEntries();
	}

	MRWindowListMode mode;
	MREditWindow *current;
	MREditWindow *preferred;
	WindowListView *listView;
	TScrollBar *scrollBar;
	TButton *hideToggleButton;
	TButton *hideAllButton;
	TButton *getButton;
	TButton *workspaceMainFileButton;
	TButton *autosaveWorkspaceButton;
	TButton *autoloadWorkspaceButton;
	WindowListCounterView *counterView;
	MREditWindow *selected;
	int lastFocusedIndex;
	std::vector<WindowListEntry> entries;
	std::vector<std::string> rows;
};
} // namespace

MREditWindow *mrShowWindowListDialog(MRWindowListMode mode, MREditWindow *current) {
	MREditWindow *preferred = mode == mrwlSelectLinkTarget ? preferredLinkTarget(current) : current;
	WindowListDialog *dialog;
	ushort result;
	MREditWindow *selected;

	if (TProgram::deskTop == nullptr) return nullptr;

	if (mode == mrwlManageWindows) {
		if (g_manageWindowListDialog != nullptr) {
			g_manageWindowListDialog->activateModeless();
			return nullptr;
		}
		dialog = new WindowListDialog(mode, current, preferred);
		dialog->finalizeLayout();
		g_manageWindowListDialog = dialog;
		TProgram::deskTop->insert(dialog);
		postWindowListActivate(dialog);
		return nullptr;
	}

	dialog = new WindowListDialog(mode, current, preferred);
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	selected = dialog->selectedWindow();
	{
		std::string line = "Window List result=" + std::to_string(result);
		line += " selected='";
		line += selected != nullptr && selected->getTitle(0) != nullptr ? selected->getTitle(0) : "";
		line += "'";
		if (selected != nullptr) {
			line += " visible=";
			line += (selected->state & sfVisible) != 0 ? "1" : "0";
			line += " selected=";
			line += (selected->state & sfSelected) != 0 ? "1" : "0";
			line += " hidden=";
			line += isWindowManuallyHidden(selected) ? "1" : "0";
		}
		mrLogMessage(line);
	}
	TObject::destroy(dialog);

	if (result == cmHelp) {
		static_cast<void>(mrShowProjectHelp());
		return nullptr;
	}
	if (result == cmCancel && mode == mrwlManageWindows && selected != nullptr) return selected;
	if (result != cmOK) return nullptr;
	return selected;
}

void mrRefreshManageWindowListDialog() {
	if (g_manageWindowListDialog != nullptr) postWindowListActivate(g_manageWindowListDialog);
}

void mrNotifyWindowTopologyChanged() {
	mrMarkWorkspaceAutosaveDirty("window topology");
	if (g_manageWindowListDialog != nullptr) message(g_manageWindowListDialog, evBroadcast, cmMrWindowTopologyChanged, nullptr);
}
