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

    TRect deskExtent = window->owner->getExtent(); // Desktop extent

    bool touchedEdge = false;
    TRect snappedBounds = window->getBounds();

    // In TView::dragView, event.mouse.where gets modified to be the top-left origin of the view.
    // So we can just check if the window bounds touched the edge.
    // The user instruction: "Werden diese mit der Maus an die Seitenränder des Desktops gezogen..."
    // Wait, let's use the window's final origin/bounds to determine if it hit an edge.

    if (snappedBounds.a.x <= deskExtent.a.x) {
        snappedBounds.a.x = deskExtent.a.x;
        snappedBounds.a.y = deskExtent.a.y;
        snappedBounds.b.x = deskExtent.b.x / 2;
        snappedBounds.b.y = deskExtent.b.y;
        touchedEdge = true;
    } else if (snappedBounds.b.x >= deskExtent.b.x) {
        snappedBounds.a.x = deskExtent.b.x / 2;
        snappedBounds.a.y = deskExtent.a.y;
        snappedBounds.b.x = deskExtent.b.x;
        snappedBounds.b.y = deskExtent.b.y;
        touchedEdge = true;
    } else if (snappedBounds.a.y <= deskExtent.a.y) {
        snappedBounds.a.x = deskExtent.a.x;
        snappedBounds.a.y = deskExtent.a.y;
        snappedBounds.b.x = deskExtent.b.x;
        snappedBounds.b.y = deskExtent.b.y / 2;
        touchedEdge = true;
    } else if (snappedBounds.b.y >= deskExtent.b.y) {
        snappedBounds.a.x = deskExtent.a.x;
        snappedBounds.a.y = deskExtent.b.y / 2;
        snappedBounds.b.x = deskExtent.b.x;
        snappedBounds.b.y = deskExtent.b.y;
        touchedEdge = true;
    }

    if (touchedEdge) {
        window->changeBounds(snappedBounds);
    }
}
