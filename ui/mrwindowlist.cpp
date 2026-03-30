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
#include <ctime>
#include <string>
#include <vector>
#include <unistd.h>

#include "TMREditWindow.hpp"
#include "mrwindowlist.hpp"

namespace {
enum : ushort {
	cmMRWindowListDelete = 200,
	cmMRWindowListSave,
	cmMRWindowListHide,
	cmMRWindowListHideAll
};

const char *kHelpWindowTitle = "MR HELP";
const char *kHelpFilePath = "mr.hlp";
const char *kLogWindowTitle = "MR LOG";

std::string g_logBuffer;

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

std::string executableDirectory() {
	char path[4096];
	ssize_t len = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
	std::size_t pos;

	if (len <= 0)
		return std::string();
	path[len] = '\0';
	pos = std::string(path).find_last_of('/');
	if (pos == std::string::npos)
		return std::string();
	return std::string(path, static_cast<std::size_t>(pos));
}

std::string resolveHelpFilePath() {
	std::string fromCwd = currentWorkingDirectory();
	std::string fromExe = executableDirectory();
	std::string candidate;

	if (!fromCwd.empty()) {
		candidate = fromCwd + "/" + kHelpFilePath;
		if (::access(candidate.c_str(), R_OK) == 0)
			return candidate;
	}
	if (!fromExe.empty()) {
		candidate = fromExe + "/" + kHelpFilePath;
		if (::access(candidate.c_str(), R_OK) == 0)
			return candidate;
	}
	return std::string(kHelpFilePath);
}

std::string currentTimestamp() {
	char buffer[32];
	std::time_t now = std::time(nullptr);
	std::tm *tmNow = std::localtime(&now);

	if (tmNow == nullptr)
		return std::string("--:--:--");
	if (std::strftime(buffer, sizeof(buffer), "%H:%M:%S", tmNow) == 0)
		return std::string("--:--:--");
	return std::string(buffer);
}

std::string normalizeLogLine(const char *message) {
	std::string line = message != nullptr ? message : "";
	while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
		line.pop_back();
	return line;
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

TMREditWindow *findWindowByTitle(const char *title) {
	std::vector<TMREditWindow *> windows = allEditWindows();
	for (std::size_t i = 0; i < windows.size(); ++i) {
		const char *windowTitle = windows[i]->getTitle(0);
		if (title != nullptr && windowTitle != nullptr && std::strcmp(windowTitle, title) == 0)
			return windows[i];
	}
	return nullptr;
}

bool isReservedUtilityWindow(TMREditWindow *win) {
	const char *title = win != nullptr ? win->getTitle(0) : nullptr;
	return title != nullptr &&
	       (std::strcmp(title, kHelpWindowTitle) == 0 || std::strcmp(title, kLogWindowTitle) == 0);
}

TMREditWindow *createDefaultWorkWindow(const char *title) {
	TRect bounds;
	TMREditWindow *win;

	if (TProgram::deskTop == nullptr)
		return nullptr;
	bounds = TProgram::deskTop->getExtent();
	bounds.grow(-2, -1);
	win = new TMREditWindow(bounds, title != nullptr ? title : "?No-File?", 1);
	TProgram::deskTop->insert(win);
	return win;
}

TMREditWindow *chooseFallbackWorkWindow() {
	std::vector<TMREditWindow *> windows = allEditWindows();
	TMREditWindow *hiddenEditable = nullptr;
	TMREditWindow *hiddenNonUtility = nullptr;

	for (std::size_t i = 0; i < windows.size(); ++i) {
		if ((windows[i]->state & sfVisible) != 0)
			return windows[i];
		if (hiddenEditable == nullptr && !windows[i]->isReadOnly())
			hiddenEditable = windows[i];
		if (hiddenNonUtility == nullptr && !isReservedUtilityWindow(windows[i]))
			hiddenNonUtility = windows[i];
	}
	if (hiddenEditable != nullptr)
		return hiddenEditable;
	if (hiddenNonUtility != nullptr)
		return hiddenNonUtility;
	return nullptr;
}

TMREditWindow *createReadOnlyTextWindow(const char *title, const char *text, bool hidden) {
	TRect bounds;
	TMREditWindow *previous;
	TMREditWindow *win;

	if (TProgram::deskTop == nullptr)
		return nullptr;

	previous = dynamic_cast<TMREditWindow *>(TProgram::deskTop->current);
	bounds = TProgram::deskTop->getExtent();
	bounds.grow(-2, -1);
	win = new TMREditWindow(bounds, title, 1);
	TProgram::deskTop->insert(win);
	if (!win->loadTextBuffer(text, title)) {
		message(win, evCommand, cmClose, nullptr);
		return nullptr;
	}
	win->setReadOnly(true);
	win->setFileChanged(false);
	if (hidden)
		win->hide();
	if (hidden && previous != nullptr && previous != win)
		mrActivateEditWindow(previous);
	return win;
}

TMREditWindow *ensureLogWindow(bool activate) {
	TMREditWindow *win = findWindowByTitle(kLogWindowTitle);

	if (g_logBuffer.empty())
		g_logBuffer = "MR/MEMAC log initialized.\n";
	if (win == nullptr)
		win = createReadOnlyTextWindow(kLogWindowTitle, g_logBuffer.c_str(), !activate);
	if (win == nullptr)
		return nullptr;
	win->replaceTextBuffer(g_logBuffer.c_str(), kLogWindowTitle);
	win->setWindowRole(TMREditWindow::wrLog);
	win->setReadOnly(true);
	win->setFileChanged(false);
	if (activate)
		mrActivateEditWindow(win);
	return win;
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
				const char *title = windows[i]->getTitle(0);
				std::string titleText = title != nullptr ? title : "";

				if (mode == mrwlSelectLinkTarget && windows[i] == current)
					continue;

				entry.window = windows[i];
				entry.fileLabel =
				    fileName.empty() ? (titleText.empty() ? "?No-File" : baseNameOf(titleText)) : baseNameOf(fileName);
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
		mrEnsureUsableWorkWindow();
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
		if ((win->state & sfVisible) == 0) {
			/* In the window list, "Hide" acts as a visibility toggle for already hidden windows. */
			selected = win;
			endModal(cmOK);
			return;
		}
		hideWindow(win);
		mrEnsureUsableWorkWindow();
		refreshEntries();
	}

	void handleHideAll() {
		std::vector<TMREditWindow *> windows = allEditWindows();
		for (std::size_t i = 0; i < windows.size(); ++i)
			hideWindow(windows[i]);
		mrEnsureUsableWorkWindow();
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
	TMREditWindow *win;
	std::string helpPath = resolveHelpFilePath();
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

	win = findWindowByTitle(kHelpWindowTitle);
	if (win == nullptr) {
		TRect bounds = TProgram::deskTop->getExtent();
		bounds.grow(-2, -1);
		win = new TMREditWindow(bounds, kHelpWindowTitle, 1);
		TProgram::deskTop->insert(win);

		if (!win->loadFromFile(helpPath.c_str())) {
			messageBox(mfError | mfOKButton, "Unable to load help file:\n%s", helpPath.c_str());
			message(win, evCommand, cmClose, nullptr);
			return false;
		}

		win->setReadOnly(true);
		win->setFileChanged(false);
	}
	win->setWindowRole(TMREditWindow::wrHelp, helpPath);
	mrActivateEditWindow(win);
	return true;
}

bool mrEnsureLogWindow(bool activate) {
	return ensureLogWindow(activate) != nullptr;
}

bool mrEnsureUsableWorkWindow() {
	TMREditWindow *current = dynamic_cast<TMREditWindow *>(TProgram::deskTop != nullptr ? TProgram::deskTop->current : nullptr);
	TMREditWindow *fallback;

	if (TProgram::deskTop == nullptr)
		return false;
	if (current != nullptr && (current->state & sfVisible) != 0)
		return true;
	fallback = chooseFallbackWorkWindow();
	if (fallback != nullptr)
		return mrActivateEditWindow(fallback);
	fallback = createDefaultWorkWindow("?No-File?");
	if (fallback == nullptr)
		return false;
	mrLogMessage("Created fallback empty window to keep the editor usable.");
	return mrActivateEditWindow(fallback);
}

void mrLogMessage(const char *message) {
	std::string line = normalizeLogLine(message);
	TMREditWindow *win;

	if (line.empty())
		return;
	if (!g_logBuffer.empty() && g_logBuffer[g_logBuffer.size() - 1] != '\n')
		g_logBuffer += '\n';
	g_logBuffer += "[" + currentTimestamp() + "] " + line + "\n";
	win = ensureLogWindow(false);
	if (win != nullptr) {
		win->replaceTextBuffer(g_logBuffer.c_str(), kLogWindowTitle);
		win->setReadOnly(true);
		win->setFileChanged(false);
	}
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
