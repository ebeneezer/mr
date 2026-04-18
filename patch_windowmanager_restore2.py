import re

with open("ui/MRWindowManager.cpp", "r") as f:
    content = f.read()

# Make sure snapping relies ONLY on mouseLocal.
print("Check window update:")
print(content.find('if (targetBounds != window->getBounds()) {'))
