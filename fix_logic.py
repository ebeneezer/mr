import re

with open("ui/MRWindowManager.cpp", "r") as f:
    content = f.read()

# Let's see what happens to the size when NOT snapped.
# In the do-while loop:
# targetBounds = originalBounds;
# newOrigin.x = ...
# targetBounds.a = newOrigin;
# targetBounds.b.x = newOrigin.x + window->size.x;
# targetBounds.b.y = newOrigin.y + window->size.y;

# BUT window->size was already updated to the snapped size during the previous locate() call!
# If it snapped, `window->size` changed.
# So if it leaves the snap zone, it calculates `targetBounds.b.x = newOrigin.x + window->size.x`
# using the SNAPPED size, not the original size!
# We should use `originalBounds.b.x - originalBounds.a.x` for the original size.
