import re

with open("ui/MRWindowManager.cpp", "r") as f:
    content = f.read()

# Replace the constraints block with TVision's moveGrow native ones
target = """            // Limit the window movement bounds just like moveGrow
            newOrigin.x = std::max(newOrigin.x, limits.a.x);
            newOrigin.y = std::max(newOrigin.y, limits.a.y);
            newOrigin.x = std::min(newOrigin.x, limits.b.x - window->size.x);
            newOrigin.y = std::min(newOrigin.y, limits.b.y - window->size.y);"""

replacement = """            // Limit the window movement bounds just like moveGrow
            newOrigin.x = std::min(std::max(newOrigin.x, limits.a.x - window->size.x + 1), limits.b.x - 1);
            newOrigin.y = std::min(std::max(newOrigin.y, limits.a.y - window->size.y + 1), limits.b.y - 1);

            if ((mode & dmLimitLoX) != 0) newOrigin.x = std::max(newOrigin.x, limits.a.x);
            if ((mode & dmLimitLoY) != 0) newOrigin.y = std::max(newOrigin.y, limits.a.y);
            if ((mode & dmLimitHiX) != 0) newOrigin.x = std::min(newOrigin.x, limits.b.x - window->size.x);
            if ((mode & dmLimitHiY) != 0) newOrigin.y = std::min(newOrigin.y, limits.b.y - window->size.y);"""

content = content.replace(target, replacement)

# Now, the snapping logic: "Snaüüing darf erst auftreten wenn man versuch die Maus weiter in Richtung des Desktop Rahmens zu ziehen"
# Meaning snapping shouldn't trigger just because mouse == 0.
# It should trigger if the user moves the mouse *outside* the deskExtent, but makeLocal usually clamps it or we just check if `currentMouse` mapped to desk is outside!
# Actually, `event.mouse.where` is global. `deskExtent` is global.
# If `event.mouse.where.x < deskExtent.a.x`, the mouse is pulled past the left edge!
# BUT `makeLocal` returns coordinates relative to `owner`. For TDeskTop, `owner` is TProgram. `makeLocal` might just be global minus owner's origin.
# So if `mouseLocal.x < 0`, it's past the left edge.
# The user wants snapping to occur ONLY when pulling the mouse outside/against the edge. So `< 0` rather than `<= 0`?
# "Snaüüing darf erst auftreten wenn man versuch die Maus weiter in Richtung des Desktop Rahmens zu ziehen. DAS ist der Auslöser , nicht einfach klicken in die an den Rahmen angrenzende Zeile oder Spalte."
# Meaning snapping happens when mouse goes OUT of bounds! i.e., x < 0 or x >= desktop.width.

snap_target = """        if (mouseLocal.x <= 0) {
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
        }"""

snap_replacement = """        if (mouseLocal.x < 0) {
            targetBounds.a.x = 0;
            targetBounds.a.y = 0;
            targetBounds.b.x = deskExtent.b.x / 2;
            targetBounds.b.y = deskExtent.b.y;
            snapped = true;
        } else if (mouseLocal.x >= deskExtent.b.x) {
            targetBounds.a.x = deskExtent.b.x / 2;
            targetBounds.a.y = 0;
            targetBounds.b.x = deskExtent.b.x;
            targetBounds.b.y = deskExtent.b.y;
            snapped = true;
        } else if (mouseLocal.y < 0) {
            targetBounds.a.x = 0;
            targetBounds.a.y = 0;
            targetBounds.b.x = deskExtent.b.x;
            targetBounds.b.y = deskExtent.b.y / 2;
            snapped = true;
        } else if (mouseLocal.y >= deskExtent.b.y) {
            targetBounds.a.x = 0;
            targetBounds.a.y = deskExtent.b.y / 2;
            targetBounds.b.x = deskExtent.b.x;
            targetBounds.b.y = deskExtent.b.y;
            snapped = true;
        }"""

content = content.replace(snap_target, snap_replacement)

with open("ui/MRWindowManager.cpp", "w") as f:
    f.write(content)
