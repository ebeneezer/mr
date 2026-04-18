import re

with open("config/MRDialogPaths.cpp", "r") as f:
    content = f.read()

# Let's search for "formatEditSetupBoolean(configuredWindowManager())"
# Wait, formatEditSetupBoolean isn't available in MRDialogPaths.cpp or maybe it is but it expects a string?
# Let's check formatEditSetupBoolean signature
