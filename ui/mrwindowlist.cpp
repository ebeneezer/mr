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
#include <tvision/tv.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

#include "TMREditWindow.hpp"
#include "mrwindowlist.hpp"

namespace {
enum : ushort {
	cmMRWindowListDelete = 3200,
	cmMRWindowListSave,
	cmMRWindowListHide,
	cmMRWindowListHideAll
};

const char *kHelpWindowTitle = "MR HELP";
const char *kHelpFilePath = "mr.hlp";

struct WindowListEntry {
	TMREditWindow *window;
	std::string fileLabel;
	std::string slotLabel;
	std::string directoryLabel;
	bool hidden;

	WindowListEntry()
	    : window(nullptr), fileLabel(), slotLabel(), directoryLabel(), hidden(false) {
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
	TFileEditor *editor;
	if (win == nullptr)
		return false;
	if (win->currentFileName()[0] != '\0')
		return false;
	editor = win->getEditor();
	return editor != nullptr && editor->bufLen == 0;
}

void collectEditWindowsProc(TView *view, void *arg) {
	std::vector<TMREditWindow *> *windows = static_cast<std::vector<TMREditWindow *> *>(arg);
	TMREditWindow *win = dynamic_cast<TMREditWindow *>(view);
	if (windows != nullptr && win != nullptr)
		windows->push_back(win);
}

std::vector<TMREditWindow *> allEditWindows() {
	std::vector<TMREditWindow *> windows;
	if (TProgram::deskTop == nullptr)
		return windows;
	/* TDeskTop stores subviews in a circular list. */
	TProgram::deskTop->forEach(collectEditWindowsProc, &windows);
	return windows;
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

	for (std::size_t i = 0; i < windows.size(); ++i) {
		if (windows[i] == current)
			continue;
		if (firstOther == nullptr)
			firstOther = windows[i];
		if (emptyUntitled == nullptr && isWindowEmptyUntitled(windows[i]))
			emptyUntitled = windows[i];
		if (!currentFile.empty() && sameFile == nullptr && currentFile == windows[i]->currentFileName())
			sameFile = windows[i];
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
	return win->saveCurrentFile();
}

void closeWindow(TMREditWindow *win) {
	if (win != nullptr)
		message(win, evCommand, cmClose, nullptr);
}

void hideWindow(TMREditWindow *win) {
	if (win != nullptr)
		win->hide();
}

class WindowListView : public TListViewer {
  public:
	WindowListView(const TRect &bounds, TScrollBar *aVScrollBar,
	               const std::vector<std::string> &aItems) noexcept
	    : TListViewer(bounds, 1, 0, aVScrollBar), items(aItems) {
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

	virtual void getText(char *dest, short item, short maxLen) override {
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

	virtual void handleEvent(TEvent &event) override {
		TListViewer::handleEvent(event);
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
	WindowListDialog(MRWindowListMode aMode, TMREditWindow *aCurrent, TMREditWindow *aPreferred)
	    : TWindowInit(&TDialog::initFrame),
	      TDialog(centeredBounds(computeWidth(), computeHeight(aMode, aCurrent)), "WINDOW LIST"),
	      mode(aMode), current(aCurrent), preferred(aPreferred), listView(nullptr),
	      scrollBar(nullptr), selected(nullptr) {
		int width = size.x;
		int height = size.y;
		int listTop = 6;
		int listBottom = height - 4;

		insert(new TButton(TRect(3, 2, 16, 4), "Delete<DEL>", cmMRWindowListDelete, bfNormal));
		insert(new TButton(TRect(18, 2, 29, 4), "Save<F3>", cmMRWindowListSave, bfNormal));
		insert(new TButton(TRect(31, 2, 49, 4), "Hide window<F4>", cmMRWindowListHide, bfNormal));
		insert(new TButton(TRect(51, 2, 66, 4), "Hide all<F5>", cmMRWindowListHideAll, bfNormal));
		insert(new TStaticText(TRect(2, 5, 18, 6), "Select window:"));

		scrollBar = new TScrollBar(TRect(width - 3, listTop, width - 2, listBottom));
		insert(scrollBar);
		listView = new WindowListView(TRect(2, listTop, width - 3, listBottom), scrollBar,
		                             std::vector<std::string>());
		insert(listView);

		insert(new TButton(TRect(22, height - 3, 35, height - 1), "OK<ENTER>", cmOK, bfDefault));
		insert(new TButton(TRect(37, height - 3, 51, height - 1), "Cancel<ESC>", cmCancel,
		                   bfNormal));
		insert(new TButton(TRect(53, height - 3, 65, height - 1), "Help<F1>", cmHelp, bfNormal));

		refreshEntries();
		focusPreferred();
		listView->select();
	}

	TMREditWindow *selectedWindow() const {
		return selected;
	}

	virtual void handleEvent(TEvent &event) override {
		TDialog::handleEvent(event);

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
					handleHide();
					clearEvent(event);
					return;
				case kbF5:
					handleHideAll();
					clearEvent(event);
					return;
				case kbF1:
					mrShowProjectHelp();
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
				handleHide();
				clearEvent(event);
				break;
			case cmMRWindowListHideAll:
				handleHideAll();
				clearEvent(event);
				break;
			case cmOK:
				selected = currentSelection();
				if (selected == nullptr)
					clearEvent(event);
				break;
			case cmHelp:
				mrShowProjectHelp();
				clearEvent(event);
				break;
		}
	}

  private:
	static int computeWidth() {
		TRect desk = TProgram::deskTop->getExtent();
		int deskWidth = desk.b.x - desk.a.x;
		return std::max(68, std::min(78, deskWidth - 2));
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

			if (mode == mrwlSelectLinkTarget && windows[i] == current)
				continue;

			entry.window = windows[i];
			entry.fileLabel = fileName.empty() ? "?No-File" : baseNameOf(fileName);
			entry.slotLabel = slotLabelFor(i);
			entry.directoryLabel = directoryOf(fileName.empty() ? currentWorkingDirectory() : fileName);
			entry.hidden = (windows[i]->state & sfVisible) == 0;
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
	}

	void focusPreferred() {
		int index = 0;
		TMREditWindow *target = nullptr;
		if (mode == mrwlActivateWindow)
			target = current;
		else
			target = preferred;
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
		hideWindow(win);
		refreshEntries();
	}

	void handleHideAll() {
		std::vector<TMREditWindow *> windows = allEditWindows();
		for (std::size_t i = 0; i < windows.size(); ++i)
			hideWindow(windows[i]);
		refreshEntries();
	}

	MRWindowListMode mode;
	TMREditWindow *current;
	TMREditWindow *preferred;
	WindowListView *listView;
	TScrollBar *scrollBar;
	TMREditWindow *selected;
	std::vector<WindowListEntry> entries;
	std::vector<std::string> rows;
};
} // namespace

bool mrActivateEditWindow(TMREditWindow *win) {
	if (win == nullptr)
		return false;
	if ((win->state & sfVisible) == 0)
		win->show();
	win->select();
	return true;
}

bool mrShowProjectHelp() {
	TRect bounds;
	TMREditWindow *win;
	if (TProgram::deskTop == nullptr)
		return false;

	win = dynamic_cast<TMREditWindow *>(TProgram::deskTop->current);
	if (win != nullptr) {
		std::string currentFile = win->currentFileName();
		const char *title = win->getTitle(0);
		if ((!currentFile.empty() && baseNameOf(currentFile) == kHelpFilePath) ||
		    (title != nullptr && std::strcmp(title, kHelpWindowTitle) == 0))
			return true;
	}

	bounds = TProgram::deskTop->getExtent();
	bounds.grow(-2, -1);
	win = new TMREditWindow(bounds, kHelpWindowTitle, 1);
	TProgram::deskTop->insert(win);

	if (!win->loadFromFile(kHelpFilePath)) {
		messageBox(mfError | mfOKButton, "Unable to load help file:\n%s", kHelpFilePath);
		message(win, evCommand, cmClose, nullptr);
		return false;
	}

	win->setFileChanged(false);
	return true;
}

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
		mrShowProjectHelp();
		return nullptr;
	}
	if (result != cmOK)
		return nullptr;
	return selected;
}
