import re

with open("ui/MRWindowManager.cpp", "r") as f:
    content = f.read()

target = """    TPoint mouseLocal = window->owner->makeLocal(event.mouse.where);
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
    }"""

replacement = """    TRect deskExtent = window->owner->getExtent(); // Desktop extent

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
    }"""

content = content.replace(target, replacement)

with open("ui/MRWindowManager.cpp", "w") as f:
    f.write(content)
