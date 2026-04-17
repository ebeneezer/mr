import re

with open("ui/MRWindowManager.cpp", "r") as f:
    content = f.read()

# Currently MRWindowManager::handleDragView relies on `window->TWindow::dragView` completely taking over the modal loop,
# and checks the final bounds afterwards.
# The user wants "live" snapping while dragging, depending ONLY on the MOUSE position, not the window bounds.
# And if the mouse moves away from the edge, it should restore the original size and continue dragging.
# To achieve this "live" preview while still maintaining the drag, we must handle the evMouseMove loop ourselves,
# just like TView::dragView does.
print("Writing a custom drag loop in handleDragView...")
