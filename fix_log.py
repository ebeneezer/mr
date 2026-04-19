import re

with open("ui/MRWindowManager.cpp", "r") as f:
    content = f.read()

target = 'TPoint mouseLocal = window->owner->makeLocal(event.mouse.where);\n    TRect deskExtent = window->owner->getExtent(); // Desktop extent\n    \n    mrLogMessage("MRWindowManager: drag finished. global mouse=(" + std::to_string(event.mouse.where.x) + "," + std::to_string(event.mouse.where.y) + ") " +\n                 "local mouse=(" + std::to_string(mouseLocal.x) + "," + std::to_string(mouseLocal.y) + ") " +\n                 "deskExtent=(" + std::to_string(deskExtent.a.x) + "," + std::to_string(deskExtent.a.y) + " to " + std::to_string(deskExtent.b.x) + "," + std::to_string(deskExtent.b.y) + ")");\n\n    TPoint mouseLocal = window->owner->makeLocal(event.mouse.where);'
replacement = 'TPoint mouseLocal = window->owner->makeLocal(event.mouse.where);\n    TRect deskExtent = window->owner->getExtent(); // Desktop extent\n    \n    mrLogMessage("MRWindowManager: drag finished. global mouse=(" + std::to_string(event.mouse.where.x) + "," + std::to_string(event.mouse.where.y) + ") " +\n                 "local mouse=(" + std::to_string(mouseLocal.x) + "," + std::to_string(mouseLocal.y) + ") " +\n                 "deskExtent=(" + std::to_string(deskExtent.a.x) + "," + std::to_string(deskExtent.a.y) + " to " + std::to_string(deskExtent.b.x) + "," + std::to_string(deskExtent.b.y) + ")");\n'

# Just rewrite properly
with open("ui/MRWindowManager.cpp", "w") as f:
    f.write("""#include "MRWindowSupport.hpp"
#include <string>
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

    mrLogMessage("MRWindowManager: drag finished. global mouse=(" + std::to_string(event.mouse.where.x) + "," + std::to_string(event.mouse.where.y) + ") " +
                 "local mouse=(" + std::to_string(mouseLocal.x) + "," + std::to_string(mouseLocal.y) + ") " +
                 "deskExtent=(" + std::to_string(deskExtent.a.x) + "," + std::to_string(deskExtent.a.y) + " to " + std::to_string(deskExtent.b.x) + "," + std::to_string(deskExtent.b.y) + ")");

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
        mrLogMessage("MRWindowManager: snappedBounds=(" + std::to_string(snappedBounds.a.x) + "," + std::to_string(snappedBounds.a.y) + " to " + std::to_string(snappedBounds.b.x) + "," + std::to_string(snappedBounds.b.y) + ")");
        window->changeBounds(snappedBounds);
    } else {
        mrLogMessage("MRWindowManager: no edge touched");
    }
}
""")
