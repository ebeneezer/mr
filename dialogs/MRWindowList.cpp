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
#define Uses_TStaticText
#define Uses_TFileDialog
#include <tvision/tv.h>

#include "MRWindowList.hpp"
#include "MRSetupCommon.hpp"
#include "../app/MRCommands.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

#include "../app/commands/MRWindowCommands.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRWindowManager.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {
enum : ushort {
	cmMRWindowListDelete = 200,
	cmMRWindowListSave,
	cmMRWindowListHide,
	cmMRWindowListHideAll,
	cmMRWindowListGet,
	cmMRWorkspaceSave,
	cmMRWorkspaceLoad,
};

class WindowListDialog;

WindowListDialog *g_manageWindowListDialog = nullptr;
constexpr const char *kHideToggleTitle = "Un/~H~ide";
constexpr const char *kHideAllTitle = "Hide ~a~ll";
constexpr const char *kRestoreTitle = "~R~estore";
constexpr const char *kRestoreAllTitle = "Restore ~a~ll";
constexpr const char *kGetTitle = "~G~et";

bool windowListDebugEnabled() noexcept {
	static int cached = -1;

	if (cached < 0) {
		const char *value = std::getenv("MR_KEY_DEBUG");
		cached = (value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0) ? 1 : 0;
	}
	return cached == 1;
}

void postWindowListClose(TView *dialog) {
	if (dialog == nullptr) return;
	TEvent closeEvent{};
	closeEvent.what = evCommand;
	closeEvent.message.command = cmClose;
	closeEvent.message.infoPtr = dialog;
	dialog->putEvent(closeEvent);
}

struct WindowListEntry {
	MREditWindow *window;
	std::string statusLabel;
	std::string desktopLabel;
	std::string fileLabel;
	std::string slotLabel;
	std::string directoryLabel;
	bool hidden;
	bool minimized;

	WindowListEntry() : window(nullptr), hidden(false), minimized(false) {
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

std::string slotLabelFor(std::size_t index) {
	char buffer[8];
	std::snprintf(buffer, sizeof(buffer), "%c", static_cast<int>('A' + (index % 26)));
	return std::string(buffer);
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

class WindowListDialog : public MRDialogFoundation {
  public:
	~WindowListDialog() override {
		if (g_manageWindowListDialog == this) g_manageWindowListDialog = nullptr;
	}

	void saveWorkspaceWithDialog() {
		char fileName[MAXPATH];

		mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::WorkspaceSave, fileName, sizeof(fileName), "*.mrmac");
		if (mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::WorkspaceSave, "*.mrmac", "Save workspace as...", "~N~ame", fdOKButton, fileName) != cmCancel) {
			std::string name(fileName);
			if (name.find(".mrmac") == std::string::npos) name += ".mrmac";
			mrSaveWorkspace(name);
		}
	}

	void loadWorkspaceWithDialog() {
		char fileName[MAXPATH];
		std::string selectedPath;
		bool readable = false;

		mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::WorkspaceLoad, fileName, sizeof(fileName), "*.mrmac");
		if (mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::WorkspaceLoad, "*.mrmac", "Load workspace from...", "~N~ame", fdOpenButton, fileName) != cmCancel) {
			selectedPath = normalizeConfiguredPathInput(fileName);
			if (selectedPath.empty()) return;
			readable = !selectedPath.empty() && ::access(selectedPath.c_str(), F_OK) == 0 && ::access(selectedPath.c_str(), R_OK) == 0;
			mrLoadWorkspace(selectedPath);
			if (readable) rememberLoadDialogPath(MRDialogHistoryScope::WorkspaceLoad, selectedPath.c_str());
			else
				forgetLoadDialogPath(MRDialogHistoryScope::WorkspaceLoad, selectedPath.c_str());
		}
	}

	WindowListDialog(MRWindowListMode aMode, MREditWindow *aCurrent, MREditWindow *aPreferred) : TWindowInit(&TDialog::initFrame), MRDialogFoundation(centeredSetupDialogRect(computeWidth(), computeHeight(aMode, aCurrent)), "WINDOW LIST", computeWidth(), computeHeight(aMode, aCurrent)), mode(aMode), current(aCurrent), preferred(aPreferred), listView(nullptr), scrollBar(nullptr), hideToggleButton(nullptr), hideAllButton(nullptr), getButton(nullptr), selected(nullptr), lastFocusedIndex(-1) {
		int width = computeWidth();
		int height = computeHeight(aMode, aCurrent);
		int listTop = 6;
		int listBottom = height - 6;
		const int topButtonY = 2;
		const int getButtonY = 4;
		const int workspaceButtonY = height - 5;
		const int bottomButtonY = height - 3;
		const int buttonGap = 2;
		const int workspaceGap = 4;
		int topButtonLeft = 2;
		int topButtonWidth = 0;
		auto centeredRowStart = [width](int contentWidth) { return std::max(2, (width - contentWidth) / 2); };

		{
			const std::array topButtons{mr::dialogs::DialogButtonSpec{"~D~elete", cmMRWindowListDelete, bfNormal}, mr::dialogs::DialogButtonSpec{"~S~ave", cmMRWindowListSave, bfNormal}, mr::dialogs::DialogButtonSpec{kHideToggleTitle, cmMRWindowListHide, bfNormal}, mr::dialogs::DialogButtonSpec{kHideAllTitle, cmMRWindowListHideAll, bfNormal}};
			const std::array widthCandidates{kHideToggleTitle, kHideAllTitle, kRestoreTitle, kRestoreAllTitle};
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
			const std::array getButtons{mr::dialogs::DialogButtonSpec{kGetTitle, cmMRWindowListGet, bfNormal}};
			std::vector<TButton *> getButtonViews;

			mr::dialogs::insertUniformButtonRow(*this, topButtonLeft, getButtonY, buttonGap, getButtons, topButtonWidth, &getButtonViews);
			if (!getButtonViews.empty()) getButton = getButtonViews[0];
		}

		scrollBar = new TScrollBar(TRect(width - 3, listTop, width - 2, listBottom));
		insert(scrollBar);
		listView = new WindowListView(TRect(2, listTop, width - 3, listBottom), scrollBar, std::vector<std::string>());
		insert(listView);

		{
			const std::array workspaceButtons{mr::dialogs::DialogButtonSpec{"Sa~v~e workspace", cmMRWorkspaceSave, bfNormal}, mr::dialogs::DialogButtonSpec{"~L~oad workspace", cmMRWorkspaceLoad, bfNormal}};
			const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(workspaceButtons, workspaceGap);
			const int left = centeredRowStart(metrics.rowWidth);

			mr::dialogs::insertUniformButtonRow(*this, left, workspaceButtonY, workspaceGap, workspaceButtons);
		}
		{
			const std::array bottomButtons{mr::dialogs::DialogButtonSpec{"~D~one", mode == mrwlManageWindows ? cmClose : cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
			const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(bottomButtons, buttonGap);
			const int left = centeredRowStart(metrics.rowWidth);

			mr::dialogs::insertUniformButtonRow(*this, left, bottomButtonY, buttonGap, bottomButtons);
		}

		refreshEntries();
		focusPreferred();
		listView->select();
		updateHideToggleState();
		setDialogValidationHook([this]() {
			DialogValidationResult result;
			result.valid = currentSelection() != nullptr;
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

	void handleEvent(TEvent &event) override {
		if (event.what == evBroadcast && event.message.command == cmMrWindowTopologyChanged) {
			if (mode == mrwlManageWindows) {
				refreshEntries();
				if (listView != nullptr) listView->drawView();
				drawView();
				if ((state & sfVisible) != 0 && TProgram::deskTop != nullptr) {
					MREditWindow *currentWindow = dynamic_cast<MREditWindow *>(TProgram::deskTop->current);
					if (currentWindow != nullptr && currentWindow->isMinimized()) activateModeless();
				}
			}
			clearEvent(event);
			return;
		}
		if (mode == mrwlManageWindows && event.what == evCommand && event.message.command == cmCancel) {
			postWindowListClose(this);
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmOK) {
			selected = currentSelection();
			if (selected == nullptr) {
				clearEvent(event);
				return;
			}
			if (mode == mrwlManageWindows) {
				if (selected->isMinimized()) selected->restoreWindow();
				static_cast<void>(mrActivateEditWindow(selected));
				clearEvent(event);
				return;
			}
		}

		MRDialogFoundation::handleEvent(event);

		if (listView != nullptr && listView->focused != lastFocusedIndex) updateHideToggleState();

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
			case cmMRWindowListHide:
				if (canToggleCurrentSelection()) handleHide();
				clearEvent(event);
				break;
			case cmMRWindowListHideAll:
				handleHideAll();
				clearEvent(event);
				break;
			case cmMRWindowListGet: {
				MREditWindow *win = currentSelection();
				if (win != nullptr && win->mVirtualDesktop != currentVirtualDesktop()) {
					TGroup *content = managedContent();
					win->mVirtualDesktop = currentVirtualDesktop();
					syncVirtualDesktopVisibility();
					MRWindowManager::handleDesktopLayoutChange();
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
		return padRight(entry.statusLabel, 5) + " " + padRight(entry.desktopLabel, 2) + " " + padRight(filePart, 24) + " " + padRight(entry.slotLabel, 2) + "  " + dirPart;
	}

	void collectEntries() {
		const std::vector<MREditWindow *> windows = allEditWindows();
		entries.clear();
		rows.clear();
		for (std::size_t i = 0; i < windows.size(); ++i) {
			WindowListEntry entry;
			std::string fileName = windows[i]->currentFileName();
			const char *title = windows[i]->getTitle(0);
			std::string titleText = title != nullptr ? title : "";

			if (mode == mrwlSelectLinkTarget && windows[i] == current) continue;

			entry.window = windows[i];
			entry.hidden = isWindowManuallyHidden(windows[i]);
			entry.minimized = windows[i]->isMinimized();
			entry.statusLabel = entry.minimized ? "[min]" : (entry.hidden ? "[hid]" : "");
			entry.desktopLabel = std::to_string(windows[i]->mVirtualDesktop);
			entry.fileLabel = fileName.empty() ? (titleText.empty() ? "?No-File" : baseNameOf(titleText)) : baseNameOf(fileName);
			entry.slotLabel = slotLabelFor(i);
			entry.directoryLabel = directoryOf(fileName.empty() ? currentWorkingDirectory() : fileName);
			entries.push_back(entry);
			rows.push_back(renderRow(entry));
		}
	}

	void sanitizeTrackedWindows() {
		const std::vector<MREditWindow *> windows = allEditWindows();
		if (!containsWindow(windows, current)) current = nullptr;
		if (!containsWindow(windows, preferred)) preferred = nullptr;
		if (!containsWindow(windows, selected)) selected = nullptr;
	}

	void refreshEntries() {
		int oldFocus = listView != nullptr ? listView->focused : 0;
		sanitizeTrackedWindows();
		collectEntries();
		if (listView != nullptr) listView->setItems(rows);
		if (entries.empty()) {
			selected = nullptr;
			if (mode == mrwlManageWindows) postWindowListClose(this);
			else
				endModal(cmCancel);
			return;
		}
		if (oldFocus < 0) oldFocus = 0;
		if (oldFocus >= static_cast<int>(entries.size())) oldFocus = static_cast<int>(entries.size()) - 1;
		listView->focusItemNum(static_cast<short>(oldFocus));
		updateHideToggleState();
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

	void handleDelete() {
		MREditWindow *win = currentSelection();
		if (win == nullptr) return;
		closeWindow(win);
		static_cast<void>(mrEnsureUsableWorkWindow(false));
		refreshEntries();
	}

	void handleSave() {
		MREditWindow *win = currentSelection();
		if (win == nullptr) return;
		saveWindow(win);
		refreshEntries();
	}

	void handleHide() {
		MREditWindow *win = currentSelection();
		std::string line;
		if (win == nullptr) return;
		if (win->isMinimized()) {
			win->restoreWindow();
			refreshEntries();
			return;
		}
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
		MREditWindow *win = currentSelection();
		return win != nullptr;
	}

	void updateHideToggleState() {
		MREditWindow *win = currentSelection();
		const bool enabled = canToggleCurrentSelection();
		const bool canGet = win != nullptr && win->mVirtualDesktop != currentVirtualDesktop();
		if (hideToggleButton != nullptr) hideToggleButton->setState(sfDisabled, enabled ? False : True);
		if (hideToggleButton != nullptr && win != nullptr) {
			const char *title = win->isMinimized() ? kRestoreTitle : kHideToggleTitle;
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
				if (!wasVisible) {
					getButton->show();
					if (content != nullptr) content->drawView();
					drawView();
				}
			} else if (wasVisible) {
				getButton->hide();
				if (content != nullptr) content->drawView();
				drawView();
			}
		}
		lastFocusedIndex = listView != nullptr ? listView->focused : -1;
	}

	void handleHideAll() {
		std::vector<MREditWindow *> windows = allEditWindows();
		if (currentSelection() != nullptr && currentSelection()->isMinimized()) {
			for (auto &window : windows)
				if (window != nullptr && window->isMinimized()) window->restoreWindow();
		} else {
			for (auto &window : windows)
				hideWindow(window);
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
		dialog->activateModeless();
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
	if (g_manageWindowListDialog != nullptr) g_manageWindowListDialog->activateModeless();
}

void mrNotifyWindowTopologyChanged() {
	if (g_manageWindowListDialog != nullptr) message(g_manageWindowListDialog, evBroadcast, cmMrWindowTopologyChanged, nullptr);
}
