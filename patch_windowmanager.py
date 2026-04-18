import re

with open("ui/MRWindowManager.cpp", "r") as f:
    content = f.read()

replacement = """#include "MRWindowManager.hpp"
#include "TMREditWindow.hpp"
#include "../config/MRDialogPaths.hpp"

#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TEvent
#include <tvision/tv.h>
#include <algorithm>

void MRWindowManager::handleDragView(TMREditWindow* window, TEvent& event, uchar mode, TRect& limits, TPoint minSize, TPoint maxSize) {
    if (!configuredWindowManager() || (mode & dmDragMove) == 0 || event.what != evMouseDown) {
        window->TWindow::dragView(event, mode, limits, minSize, maxSize);
        return;
    }

    TRect originalBounds = window->getBounds();
    TRect currentBounds = originalBounds;
    TPoint mouseStart = event.mouse.where;

    // Offset between mouse click and top-left corner
    TPoint offset = window->origin - event.mouse.where;

    window->setState(sfDragging, True);

    do {
        TPoint currentMouse = event.mouse.where;
        TPoint mouseLocal = window->owner->makeLocal(currentMouse);
        TRect deskExtent = window->owner->getExtent();

        bool snapped = false;
        TRect targetBounds = currentBounds;

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
            // Normal drag movement
            TPoint newOrigin = currentMouse + offset;

            // Constrain origin within limits
            newOrigin.x = std::max(newOrigin.x, limits.a.x);
            newOrigin.y = std::max(newOrigin.y, limits.a.y);
            newOrigin.x = std::min(newOrigin.x, limits.b.x - originalBounds.b.x + originalBounds.a.x);
            newOrigin.y = std::min(newOrigin.y, limits.b.y - originalBounds.b.y + originalBounds.a.y);

            targetBounds.a = newOrigin;
            targetBounds.b.x = newOrigin.x + (originalBounds.b.x - originalBounds.a.x);
            targetBounds.b.y = newOrigin.y + (originalBounds.b.y - originalBounds.a.y);
        }

        if (targetBounds != window->getBounds()) {
            window->locate(targetBounds); // Use locate for visual dragging shadow in TVision
        }

    } while (window->mouseEvent(event, evMouseMove));

    window->setState(sfDragging, False);
}
"""

with open("ui/MRWindowManager.cpp", "w") as f:
    f.write(replacement)
