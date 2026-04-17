#include "MRWindowManager.hpp"
#include "TMREditWindow.hpp"
#include "../config/MRDialogPaths.hpp"

#define Uses_TProgram
#define Uses_TDeskTop
#include <tvision/tv.h>
#include <algorithm>

void MRWindowManager::handleDragView(TMREditWindow* window, TEvent& event, uchar mode, TRect& limits, TPoint minSize, TPoint maxSize) {
    if (!configuredWindowManager()) {
        window->TWindow::dragView(event, mode, limits, minSize, maxSize);
        return;
    }

    window->TWindow::dragView(event, mode, limits, minSize, maxSize);

    // Only snap on move, not grow
    if ((mode & dmDragMove) == 0) {
        return;
    }

    TPoint mouseLocal = window->owner->makeLocal(event.mouse.where);
    TRect deskExtent = window->owner->getExtent(); // Desktop extent

    bool touchedEdge = false;
    TRect snappedBounds = window->getBounds();

    if (mouseLocal.x <= 0) {
        snappedBounds.a.x = 0;
        snappedBounds.a.y = 0;
        snappedBounds.b.x = deskExtent.b.x / 2;
        snappedBounds.b.y = deskExtent.b.y;
        touchedEdge = true;
    } else if (mouseLocal.x >= deskExtent.b.x - 1) {
        snappedBounds.a.x = deskExtent.b.x / 2;
        snappedBounds.a.y = 0;
        snappedBounds.b.x = deskExtent.b.x;
        snappedBounds.b.y = deskExtent.b.y;
        touchedEdge = true;
    } else if (mouseLocal.y <= 0) {
        snappedBounds.a.x = 0;
        snappedBounds.a.y = 0;
        snappedBounds.b.x = deskExtent.b.x;
        snappedBounds.b.y = deskExtent.b.y / 2;
        touchedEdge = true;
    } else if (mouseLocal.y >= deskExtent.b.y - 1) {
        snappedBounds.a.x = 0;
        snappedBounds.a.y = deskExtent.b.y / 2;
        snappedBounds.b.x = deskExtent.b.x;
        snappedBounds.b.y = deskExtent.b.y;
        touchedEdge = true;
    }

    if (touchedEdge) {
        window->changeBounds(snappedBounds);
    }
}
