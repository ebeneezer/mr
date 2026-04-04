#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TEvent
#include <tvision/tv.h>

#include "MRWindowCommands.hpp"

#include <limits>
#include <set>
#include <vector>

#include "MRDialogPaths.hpp"
#include "../../ui/TMREditWindow.hpp"
#include "../../ui/MRWindowSupport.hpp"

namespace {
void collectEditWindowsInZOrder(TView *view, void *arg) {
	std::vector<TMREditWindow *> *windows = static_cast<std::vector<TMREditWindow *> *>(arg);
	TMREditWindow *win = dynamic_cast<TMREditWindow *>(view);

	if (windows != nullptr && win != nullptr)
		windows->push_back(win);
}
} // namespace

std::vector<TMREditWindow *> allEditWindowsInZOrder() {
	std::vector<TMREditWindow *> windows;

	if (TProgram::deskTop == nullptr)
		return windows;

	TProgram::deskTop->forEach(collectEditWindowsInZOrder, &windows);
	return windows;
}

namespace {
short nextEditorWindowNumber() {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	std::set<short> used;
	short candidate = 1;

	for (auto & window : windows) {
		if (window != nullptr && window->number > 0)
			used.insert(window->number);
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

	if (TProgram::deskTop == nullptr)
		return nullptr;
	bounds = TProgram::deskTop->getExtent();
	bounds.grow(-2, -1);
	win = new TMREditWindow(bounds, title, nextEditorWindowNumber());
	TProgram::deskTop->insert(win);
	if (win != nullptr)
		win->flags |= (wfMove | wfGrow | wfZoom | wfClose);
	if (win != nullptr && win->getEditor() != nullptr)
		win->getEditor()->setInsertModeEnabled(configuredDefaultInsertMode());
	return win;
}

TMREditWindow *currentEditWindow() {
	if (TProgram::deskTop == nullptr || TProgram::deskTop->current == nullptr)
		return nullptr;
	return dynamic_cast<TMREditWindow *>(TProgram::deskTop->current);
}

TMREditWindow *findEditWindowByBufferId(int bufferId) {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	for (auto & window : windows)
		if (window != nullptr && window->bufferId() == bufferId)
			return window;
	return nullptr;
}

bool isEmptyUntitledEditableWindow(TMREditWindow *win) {
	if (win == nullptr || win->isReadOnly() || win->currentFileName()[0] != '\0' || win->isFileChanged())
		return false;
	return win->isBufferEmpty();
}

TMREditWindow *findReusableEmptyWindow(TMREditWindow *preferred) {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	if (preferred != nullptr && isEmptyUntitledEditableWindow(preferred))
		return preferred;
	for (auto & window : windows)
		if (isEmptyUntitledEditableWindow(window))
			return window;
	return nullptr;
}

bool closeCurrentEditWindow() {
	TMREditWindow *win = currentEditWindow();
	if (win == nullptr)
		return false;
	message(win, evCommand, cmClose, nullptr);
	return mrEnsureUsableWorkWindow();
}

bool activateRelativeEditWindow(int delta) {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	TMREditWindow *current = currentEditWindow();
	std::size_t index;

	if (windows.empty())
		return false;
	if (current == nullptr)
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
	if (win == nullptr)
		return false;
	win->hide();
	return mrEnsureUsableWorkWindow();
}
