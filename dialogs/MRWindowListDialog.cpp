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

#include "MRWindowListDialog.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

#include "../app/commands/MRWindowCommands.hpp"
#include "../ui/TMREditWindow.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {
enum : ushort {
	cmMRWindowListDelete = 200,
	cmMRWindowListSave,
	cmMRWindowListHide,
	cmMRWindowListHideAll,
	cmMRWorkspaceSave,
	cmMRWorkspaceLoad,
};

struct WindowListEntry {
	TMREditWindow *window;
	std::string fileLabel;
	std::string slotLabel;
	std::string directoryLabel;
	bool hidden;

	WindowListEntry()
	    : window(nullptr),  hidden(false) {
	}
};

std::string currentWorkingDirectory() {
	char cwd[1024];
	if (::getcwd(cwd, sizeof(cwd)) == nullptr)
		return std::string();
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
	if (pos == std::string::npos)
		return path;
	return path.substr(pos + 1);
}

std::string directoryOf(const std::string &path) {
	std::size_t pos = path.find_last_of("\\/");
	if (pos == std::string::npos)
		return currentWorkingDirectory();
	if (pos == 2 && path[1] == ':')
		return path.substr(0, pos + 1);
	if (pos == 0)
		return path.substr(0, 1);
	return path.substr(0, pos);
}

std::string padRight(const std::string &value, std::size_t width) {
	if (value.size() >= width)
		return value.substr(0, width);
	return value + std::string(width - value.size(), ' ');
}

std::string slotLabelFor(std::size_t index) {
	char buffer[8];
	std::snprintf(buffer, sizeof(buffer), "%c", static_cast<int>('A' + (index % 26)));
	return std::string(buffer);
}

bool isWindowEmptyUntitled(TMREditWindow *win) {
	if (win == nullptr)
		return false;
	if (win->currentFileName()[0] != '\0')
		return false;
	return win->isBufferEmpty();
}

std::vector<TMREditWindow *> allEditWindows() {
	return allEditWindowsInZOrder();
}

TMREditWindow *preferredLinkTarget(TMREditWindow *current) {
	std::vector<TMREditWindow *> windows = allEditWindows();
	TMREditWindow *firstOther = nullptr;
	TMREditWindow *emptyUntitled = nullptr;
	TMREditWindow *sameFile = nullptr;
	std::string currentFile;

	if (current == nullptr)
		return nullptr;
	currentFile = current->currentFileName();

	for (auto & window : windows) {
		if (window == current)
			continue;
		if (firstOther == nullptr)
			firstOther = window;
		if (emptyUntitled == nullptr && isWindowEmptyUntitled(window))
			emptyUntitled = window;
		if (!currentFile.empty() && sameFile == nullptr && currentFile == window->currentFileName())
			sameFile = window;
	}
	if (emptyUntitled != nullptr)
		return emptyUntitled;
	if (sameFile != nullptr)
		return sameFile;
	return firstOther;
}

bool saveWindow(TMREditWindow *win) {
	if (win == nullptr)
		return false;
	if (win->isReadOnly()) {
		messageBox(mfInformation | mfOKButton, "Window is read-only.");
		mrLogMessage("Save rejected for read-only window.");
		return false;
	}
	if (!win->isFileChanged())
		return true;
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

void closeWindow(TMREditWindow *win) {
	if (win != nullptr)
		message(win, evCommand, cmClose, nullptr);
}

void hideWindow(TMREditWindow *win) {
	if (win != nullptr) {
		setWindowManuallyHidden(win, true);
		win->hide();
	}
}

class WindowListView : public TListViewer {
  public:
	WindowListView(const TRect &bounds, TScrollBar *aVScrollBar,
	               const std::vector<std::string> &aItems) noexcept
	    : TListViewer(bounds, 1, nullptr, aVScrollBar), items(aItems) {
		setRange(static_cast<short>(items.size()));
	}

	void setItems(const std::vector<std::string> &aItems) {
		items = aItems;
		setRange(static_cast<short>(items.size()));
		if (items.empty())
			focusItemNum(0);
		else if (focused >= range)
			focusItemNum(range - 1);
	}

	void getText(char *dest, short item, short maxLen) override {
		std::size_t copyLen;

		if (dest == nullptr || maxLen <= 0)
			return;
		if (item < 0 || static_cast<std::size_t>(item) >= items.size()) {
			dest[0] = EOS;
			return;
		}

		copyLen = static_cast<std::size_t>(maxLen - 1);
		std::strncpy(dest, items[item].c_str(), copyLen);
		dest[copyLen] = EOS;
	}

	void handleEvent(TEvent &event) override {
		const bool isDoubleClickActivation =
		    event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0 &&
		    (event.mouse.eventFlags & meDoubleClick) != 0;

		TListViewer::handleEvent(event);
		if (isDoubleClickActivation && focused >= 0 && focused < range) {
			message(owner, evCommand, cmOK, nullptr);
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter && focused >= 0 &&
		    focused < range) {
			message(owner, evCommand, cmOK, nullptr);
			clearEvent(event);
		}
	}

  private:
	std::vector<std::string> items;
};

class WindowListDialog : public TDialog {
  public:
	void saveWorkspaceWithDialog() {
		char fileName[MAXPATH];

		initRememberedLoadDialogPath(fileName, sizeof(fileName), "*.mrmac");
		if (TEditor::editorDialog(edSaveAs, fileName) != cmCancel) {
			std::string name(fileName);
			if (name.find(".mrmac") == std::string::npos)
				name += ".mrmac";
			mrSaveWorkspace(name);
		}
	}

	void loadWorkspaceWithDialog() {
		char fileName[MAXPATH];

		fileName[0] = '\0';
		initRememberedLoadDialogPath(fileName, sizeof(fileName), "*.mrmac");
		TFileDialog *dialog =
		    new TFileDialog(fileName, "Load workspace from...", "~N~ame", fdOpenButton, kFileDialogHistoryId);
		if (TProgram::deskTop->execView(dialog) != cmCancel) {
			dialog->getFileName(fileName);
			mrLoadWorkspace(std::string(fileName));
		}
		TObject::destroy(dialog);
	}

	WindowListDialog(MRWindowListMode aMode, TMREditWindow *aCurrent, TMREditWindow *aPreferred)
	    : TWindowInit(&TDialog::initFrame),
	      TDialog(centeredBounds(computeWidth(), computeHeight(aMode, aCurrent)), "WINDOW LIST"),
	      mode(aMode), current(aCurrent), preferred(aPreferred), listView(nullptr),
	      scrollBar(nullptr), hideToggleButton(nullptr), selected(nullptr), lastFocusedIndex(-1) {
		int width = size.x;
		int height = size.y;
		int listTop = 6;
		int listBottom = height - 6;
		const int topButtonY = 2;
		const int workspaceButtonY = height - 5;
		const int bottomButtonY = height - 3;
		const int buttonGap = 2;
		const int workspaceGap = 4;
		auto centeredRowStart = [width](int contentWidth) {
			return std::max(2, (width - contentWidth) / 2);
		};

		{
			const int deleteWidth = 13;
			const int saveWidth = 11;
			const int hideWidth = 18;
			const int hideAllWidth = 15;
			const int rowWidth = deleteWidth + saveWidth + hideWidth + hideAllWidth + buttonGap * 3;
			int x = centeredRowStart(rowWidth);

			insert(new TButton(TRect(x, topButtonY, x + deleteWidth, topButtonY + 2), "~D~elete<DEL>",
			                   cmMRWindowListDelete, bfNormal));
			x += deleteWidth + buttonGap;
			insert(new TButton(TRect(x, topButtonY, x + saveWidth, topButtonY + 2), "~S~ave<F3>",
			                   cmMRWindowListSave, bfNormal));
			x += saveWidth + buttonGap;
			hideToggleButton = new TButton(TRect(x, topButtonY, x + hideWidth, topButtonY + 2),
			                               "Un/~H~ide<F4>", cmMRWindowListHide, bfNormal);
			insert(hideToggleButton);
			x += hideWidth + buttonGap;
			insert(new TButton(TRect(x, topButtonY, x + hideAllWidth, topButtonY + 2), "Hide ~a~ll<F5>",
			                   cmMRWindowListHideAll, bfNormal));
		}
		insert(new TStaticText(TRect(2, 5, 18, 6), "Select window:"));

		scrollBar = new TScrollBar(TRect(width - 3, listTop, width - 2, listBottom));
		insert(scrollBar);
		listView = new WindowListView(TRect(2, listTop, width - 3, listBottom), scrollBar,
		                              std::vector<std::string>());
		insert(listView);

		{
			const int wsButtonWidth = 18;
			const int wsRowWidth = wsButtonWidth * 2 + workspaceGap;
			int x = centeredRowStart(wsRowWidth);

			insert(new TButton(TRect(x, workspaceButtonY, x + wsButtonWidth, workspaceButtonY + 2),
			                   "Sa~v~e workspace<F6>", cmMRWorkspaceSave, bfNormal));
			x += wsButtonWidth + workspaceGap;
			insert(new TButton(TRect(x, workspaceButtonY, x + wsButtonWidth, workspaceButtonY + 2),
			                   "~L~oad workspace<F7>", cmMRWorkspaceLoad, bfNormal));
		}
		{
			const int okWidth = 13;
			const int cancelWidth = 14;
			const int helpWidth = 12;
			const int rowWidth = okWidth + cancelWidth + helpWidth + buttonGap * 2;
			int x = centeredRowStart(rowWidth);

			insert(new TButton(TRect(x, bottomButtonY, x + okWidth, bottomButtonY + 2), "~O~K<ENTER>", cmOK,
			                   bfDefault));
			x += okWidth + buttonGap;
			insert(new TButton(TRect(x, bottomButtonY, x + cancelWidth, bottomButtonY + 2),
			                   "~C~ancel<ESC>", cmCancel, bfNormal));
			x += cancelWidth + buttonGap;
			insert(new TButton(TRect(x, bottomButtonY, x + helpWidth, bottomButtonY + 2), "~H~elp<F1>",
			                   cmHelp, bfNormal));
		}

		refreshEntries();
		focusPreferred();
		listView->select();
		updateHideToggleState();
	}

	TMREditWindow *selectedWindow() const {
		if (selected != nullptr)
			return selected;
		return currentSelection();
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evCommand && event.message.command == cmOK) {
			selected = currentSelection();
			if (selected == nullptr) {
				clearEvent(event);
				return;
			}
		}

		TDialog::handleEvent(event);

		if (listView != nullptr && listView->focused != lastFocusedIndex)
			updateHideToggleState();

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
					if (canToggleCurrentSelection())
						handleHide();
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

		if (event.what != evCommand)
			return;

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
				if (canToggleCurrentSelection())
					handleHide();
				clearEvent(event);
				break;
			case cmMRWindowListHideAll:
				handleHideAll();
				clearEvent(event);
				break;
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

	static int computeHeight(MRWindowListMode, TMREditWindow *) {
		std::size_t count = allEditWindows().size();
		int visibleRows = std::max<int>(4, std::min<int>(static_cast<int>(count), 10));
		TRect desk = TProgram::deskTop->getExtent();
		int deskHeight = desk.b.y - desk.a.y;
		int desired = visibleRows + 10;
		return std::max(12, std::min(desired, deskHeight - 1));
	}

	static TRect centeredBounds(int width, int height) {
		TRect desk = TProgram::deskTop->getExtent();
		int left = desk.a.x + (desk.b.x - desk.a.x - width) / 2;
		int top = desk.a.y + (desk.b.y - desk.a.y - height) / 2;
		return TRect(left, top, left + width, top + height);
	}

	std::string renderRow(const WindowListEntry &entry) const {
		std::string filePart = entry.fileLabel;
		std::string dirPart = trimCopy(entry.directoryLabel);
		if (entry.hidden)
			dirPart += " [hidden]";
		return padRight(filePart, 24) + " " + padRight(entry.slotLabel, 2) + "  " + dirPart;
	}

	void collectEntries() {
		std::vector<TMREditWindow *> windows = allEditWindows();
		entries.clear();
		rows.clear();
		for (std::size_t i = 0; i < windows.size(); ++i) {
			WindowListEntry entry;
			std::string fileName = windows[i]->currentFileName();
			const char *title = windows[i]->getTitle(0);
			std::string titleText = title != nullptr ? title : "";

			if (mode == mrwlSelectLinkTarget && windows[i] == current)
				continue;

			entry.window = windows[i];
			entry.fileLabel =
			    fileName.empty() ? (titleText.empty() ? "?No-File" : baseNameOf(titleText)) : baseNameOf(fileName);
			entry.slotLabel = slotLabelFor(i);
			entry.directoryLabel = directoryOf(fileName.empty() ? currentWorkingDirectory() : fileName);
			entry.hidden = isWindowManuallyHidden(windows[i]);
			entries.push_back(entry);
			rows.push_back(renderRow(entry));
		}
	}

	void refreshEntries() {
		int oldFocus = listView != nullptr ? listView->focused : 0;
		collectEntries();
		if (listView != nullptr)
			listView->setItems(rows);
		if (entries.empty()) {
			selected = nullptr;
			endModal(cmCancel);
			return;
		}
		if (oldFocus < 0)
			oldFocus = 0;
		if (oldFocus >= static_cast<int>(entries.size()))
			oldFocus = static_cast<int>(entries.size()) - 1;
		listView->focusItemNum(static_cast<short>(oldFocus));
		updateHideToggleState();
	}

	void focusPreferred() {
		int index = 0;
		TMREditWindow *target = mode == mrwlActivateWindow ? current : preferred;
		for (std::size_t i = 0; i < entries.size(); ++i) {
			if (entries[i].window == target) {
				index = static_cast<int>(i);
				break;
			}
		}
		if (!entries.empty())
			listView->focusItemNum(static_cast<short>(index));
	}

	TMREditWindow *currentSelection() const {
		if (listView == nullptr || listView->focused < 0 ||
		    static_cast<std::size_t>(listView->focused) >= entries.size())
			return nullptr;
		return entries[static_cast<std::size_t>(listView->focused)].window;
	}

	void handleDelete() {
		TMREditWindow *win = currentSelection();
		if (win == nullptr)
			return;
		closeWindow(win);
		static_cast<void>(mrEnsureUsableWorkWindow());
		refreshEntries();
	}

	void handleSave() {
		TMREditWindow *win = currentSelection();
		if (win == nullptr)
			return;
		saveWindow(win);
		refreshEntries();
	}

	void handleHide() {
		TMREditWindow *win = currentSelection();
		if (win == nullptr)
			return;
		if ((win->state & sfVisible) != 0) {
			hideWindow(win);
			static_cast<void>(mrEnsureUsableWorkWindow());
		}
		else {
			setWindowManuallyHidden(win, false);
			static_cast<void>(mrActivateEditWindow(win));
		}
		refreshEntries();
	}

	bool canToggleCurrentSelection() const {
		TMREditWindow *win = currentSelection();
		return win != nullptr;
	}

	void updateHideToggleState() {
		const bool enabled = canToggleCurrentSelection();
		if (hideToggleButton != nullptr)
			hideToggleButton->setState(sfDisabled, enabled ? False : True);
		lastFocusedIndex = listView != nullptr ? listView->focused : -1;
	}

	void handleHideAll() {
		std::vector<TMREditWindow *> windows = allEditWindows();
		for (auto & window : windows)
			hideWindow(window);
		static_cast<void>(mrEnsureUsableWorkWindow());
		refreshEntries();
	}

	MRWindowListMode mode;
	TMREditWindow *current;
	TMREditWindow *preferred;
	WindowListView *listView;
	TScrollBar *scrollBar;
	TButton *hideToggleButton;
	TMREditWindow *selected;
	int lastFocusedIndex;
	std::vector<WindowListEntry> entries;
	std::vector<std::string> rows;
};
} // namespace

TMREditWindow *mrShowWindowListDialog(MRWindowListMode mode, TMREditWindow *current) {
	TMREditWindow *preferred = mode == mrwlSelectLinkTarget ? preferredLinkTarget(current) : current;
	WindowListDialog *dialog;
	ushort result;
	TMREditWindow *selected;

	if (TProgram::deskTop == nullptr)
		return nullptr;

	dialog = new WindowListDialog(mode, current, preferred);
	result = TProgram::deskTop->execView(dialog);
	selected = dialog->selectedWindow();
	TObject::destroy(dialog);

	if (result == cmHelp) {
		static_cast<void>(mrShowProjectHelp());
		return nullptr;
	}
	if (result != cmOK)
		return nullptr;
	return selected;
}
