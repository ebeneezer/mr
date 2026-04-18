import re

with open("ui/MRWindowManager.cpp", "r") as f:
    content = f.read()

target = """        if (!snapped) {
            TPoint newOrigin = currentMouse + offset;

            // Limit the window movement bounds just like moveGrow
            newOrigin.x = std::min(std::max(newOrigin.x, limits.a.x - window->size.x + 1), limits.b.x - 1);
            newOrigin.y = std::min(std::max(newOrigin.y, limits.a.y - window->size.y + 1), limits.b.y - 1);

            if ((mode & dmLimitLoX) != 0) newOrigin.x = std::max(newOrigin.x, limits.a.x);
            if ((mode & dmLimitLoY) != 0) newOrigin.y = std::max(newOrigin.y, limits.a.y);
            if ((mode & dmLimitHiX) != 0) newOrigin.x = std::min(newOrigin.x, limits.b.x - window->size.x);
            if ((mode & dmLimitHiY) != 0) newOrigin.y = std::min(newOrigin.y, limits.b.y - window->size.y);

            targetBounds.a = newOrigin;
            targetBounds.b.x = newOrigin.x + window->size.x;
            targetBounds.b.y = newOrigin.y + window->size.y;
        }"""

replacement = """        if (!snapped) {
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
        }"""

content = content.replace(target, replacement)

# Now, "Snapping tritt nicht auf an den linken und rechten Rändern des Desktops"
# Ah, if mouseLocal.x < 0 OR mouseLocal.x >= deskExtent.b.x
# deskExtent for left/right usually includes the whole screen.
# deskExtent.b.x is usually the width (e.g. 80).
# If the user drags to the right edge, does makeLocal(currentMouse).x become >= 80?
# Sometimes `event.mouse.where` doesn't go past the last column (e.g., it stops at 79).
# If TDeskTop extent is 80, the max mouse.x is 79!
# So `mouseLocal.x >= deskExtent.b.x` would be 79 >= 80 (false)!
# Let's fix the snapping triggers to correctly detect when mouse reaches the absolute edge.
# Left edge: mouseLocal.x <= 0 (since it can't go below 0)
# Right edge: mouseLocal.x >= deskExtent.b.x - 1
# Top edge: mouseLocal.y <= 0
# Bottom edge: mouseLocal.y >= deskExtent.b.y - 1

snap_target = """        if (mouseLocal.x < 0) {
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

snap_replacement = """        if (mouseLocal.x <= 0) {
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

content = content.replace(snap_target, snap_replacement)

with open("ui/MRWindowManager.cpp", "w") as f:
    f.write(content)
