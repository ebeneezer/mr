#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TEvent
#include <tvision/tv.h>

#include "MRWindowCommands.hpp"

#include <limits>
#include <set>
#include <vector>

#include "../ui/TMREditWindow.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {
void collectEditWindowsInZOrder(TView *view, void *arg) {
	std::vector<TMREditWindow *> *windows = static_cast<std::vector<TMREditWindow *> *>(arg);
	TMREditWindow *win = dynamic_cast<TMREditWindow *>(view);

	if (windows != 0 && win != 0)
		windows->push_back(win);
}
} // namespace

std::vector<TMREditWindow *> allEditWindowsInZOrder() {
	std::vector<TMREditWindow *> windows;

	if (TProgram::deskTop == 0)
		return windows;

	TProgram::deskTop->forEach(collectEditWindowsInZOrder, &windows);
	return windows;
}

namespace {
short nextEditorWindowNumber() {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	std::set<short> used;
	short candidate = 1;

	for (std::size_t i = 0; i < windows.size(); ++i) {
		if (windows[i] != 0 && windows[i]->number > 0)
			used.insert(windows[i]->number);
	}

	while (used.find(candidate) != used.end()) {
		if (candidate == std::numeric_limits<short>::max())
			return candidate;
		++candidate;
	}
	return candidate;
}
} // namespace

TMREditWindow *createEditorWindow(const char *title) {
	TRect bounds;
	TMREditWindow *win;

	if (TProgram::deskTop == 0)
		return 0;
	bounds = TProgram::deskTop->getExtent();
	bounds.grow(-2, -1);
	win = new TMREditWindow(bounds, title, nextEditorWindowNumber());
	TProgram::deskTop->insert(win);
	return win;
}

TMREditWindow *currentEditWindow() {
	if (TProgram::deskTop == 0 || TProgram::deskTop->current == 0)
		return 0;
	return dynamic_cast<TMREditWindow *>(TProgram::deskTop->current);
}

TMREditWindow *findEditWindowByBufferId(int bufferId) {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	for (std::size_t i = 0; i < windows.size(); ++i)
		if (windows[i] != 0 && windows[i]->bufferId() == bufferId)
			return windows[i];
	return 0;
}

bool isEmptyUntitledEditableWindow(TMREditWindow *win) {
	if (win == 0 || win->isReadOnly() || win->currentFileName()[0] != '\0' || win->isFileChanged())
		return false;
	return win->isBufferEmpty();
}

TMREditWindow *findReusableEmptyWindow(TMREditWindow *preferred) {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	if (preferred != 0 && isEmptyUntitledEditableWindow(preferred))
		return preferred;
	for (std::size_t i = 0; i < windows.size(); ++i)
		if (isEmptyUntitledEditableWindow(windows[i]))
			return windows[i];
	return 0;
}

bool closeCurrentEditWindow() {
	TMREditWindow *win = currentEditWindow();
	if (win == 0)
		return false;
	message(win, evCommand, cmClose, 0);
	return mrEnsureUsableWorkWindow();
}

bool activateRelativeEditWindow(int delta) {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	TMREditWindow *current = currentEditWindow();
	std::size_t index;

	if (windows.empty())
		return false;
	if (current == 0)
		return mrActivateEditWindow(windows.front());

	for (index = 0; index < windows.size(); ++index) {
		if (windows[index] == current) {
			int nextIndex = static_cast<int>(index) + delta;
			int count = static_cast<int>(windows.size());

			while (nextIndex < 0)
				nextIndex += count;
			nextIndex %= count;
			return mrActivateEditWindow(windows[static_cast<std::size_t>(nextIndex)]);
		}
	}
	return mrActivateEditWindow(windows.front());
}

bool hideCurrentEditWindow() {
	TMREditWindow *win = currentEditWindow();
	if (win == 0)
		return false;
	win->hide();
	return mrEnsureUsableWorkWindow();
}
