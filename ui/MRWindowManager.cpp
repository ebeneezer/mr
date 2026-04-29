#include "MRWindowManager.hpp"
#include "MREditWindow.hpp"
#include "../config/MRDialogPaths.hpp"

#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TEvent
#include <tvision/tv.h>
#include <algorithm>

void MRWindowManager::handleDragView(MREditWindow *window, TEvent &event, uchar mode, TRect &limits, TPoint minSize, TPoint maxSize) {
	if (!configuredWindowManager() || (mode & dmDragMove) == 0 || event.what != evMouseDown) {
		window->TWindow::dragView(event, mode, limits, minSize, maxSize);
		return;
	}

	TRect originalBounds = window->getBounds();
	TPoint offset = window->origin - event.mouse.where;

	window->setState(sfDragging, True);

	do {
		TPoint currentMouse = event.mouse.where;
		TPoint mouseLocal = window->owner->makeLocal(currentMouse);
		TRect deskExtent = window->owner->getExtent();

		bool snapped = false;
		TRect targetBounds = originalBounds;

		if (mouseLocal.x <= 0) {
			targetBounds.a.x = 0;
			targetBounds.a.y = 0;
			targetBounds.b.x = deskExtent.b.x / 2;
			targetBounds.b.y = deskExtent.b.y;
			snapped = true;
		} else if (mouseLocal.x >= deskExtent.b.x - 1) {
			targetBounds.a.x = deskExtent.b.x / 2;
			targetBounds.a.y = 0;
			targetBounds.b.x = deskExtent.b.x;
			targetBounds.b.y = deskExtent.b.y;
			snapped = true;
		} else if (mouseLocal.y <= 0) {
			targetBounds.a.x = 0;
			targetBounds.a.y = 0;
			targetBounds.b.x = deskExtent.b.x;
			targetBounds.b.y = deskExtent.b.y / 2;
			snapped = true;
		} else if (mouseLocal.y >= deskExtent.b.y - 1) {
			targetBounds.a.x = 0;
			targetBounds.a.y = deskExtent.b.y / 2;
			targetBounds.b.x = deskExtent.b.x;
			targetBounds.b.y = deskExtent.b.y;
			snapped = true;
		}

		if (!snapped) {
			TPoint newOrigin = currentMouse + offset;
			TPoint origSize;
			origSize.x = originalBounds.b.x - originalBounds.a.x;
			origSize.y = originalBounds.b.y - originalBounds.a.y;

			// Limit the window movement bounds just like moveGrow
			newOrigin.x = std::min(std::max(newOrigin.x, limits.a.x - origSize.x + 1), limits.b.x - 1);
			newOrigin.y = std::min(std::max(newOrigin.y, limits.a.y - origSize.y + 1), limits.b.y - 1);

			if ((mode & dmLimitLoX) != 0) newOrigin.x = std::max(newOrigin.x, limits.a.x);
			if ((mode & dmLimitLoY) != 0) newOrigin.y = std::max(newOrigin.y, limits.a.y);
			if ((mode & dmLimitHiX) != 0) newOrigin.x = std::min(newOrigin.x, limits.b.x - origSize.x);
			if ((mode & dmLimitHiY) != 0) newOrigin.y = std::min(newOrigin.y, limits.b.y - origSize.y);

			targetBounds.a = newOrigin;
			targetBounds.b.x = newOrigin.x + origSize.x;
			targetBounds.b.y = newOrigin.y + origSize.y;
		}

		if (targetBounds != window->getBounds()) {
			window->locate(targetBounds);
		}

	} while (window->mouseEvent(event, evMouseMove));

	window->setState(sfDragging, False);
}
