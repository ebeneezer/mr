import re

with open("ui/MRWindowManager.cpp", "r") as f:
    content = f.read()

# Let's inspect the targetBounds constraint logic.
# I had:
# newOrigin.x = std::max(newOrigin.x, limits.a.x);
# newOrigin.x = std::min(newOrigin.x, limits.b.x - window->size.x);
# This limits the window to be FULLY visible inside limits.
# The original TView::moveGrow limits origin:
# p.x = min(max(p.x, limits.a.x - s.x+1), limits.b.x-1);
# This allows the window to go mostly off-screen!
# I need to restore the correct constraint logic.
