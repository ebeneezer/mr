import re

with open("dialogs/MRSetupDialogs.cpp", "r") as f:
    content = f.read()

# wait, I used `dialog->getData(&cbData)` but `dialog` is an `MRScrollableDialog*` !
# MRScrollableDialog doesn't implement `getData` that reads from managed views automatically, it inherits from TDialog which has a specific `getData`.
# `TDialog::getData` reads from all views that have `ofSelectable` or something if it's set up correctly, but `MRScrollableDialog` might not do it properly if it puts them in a sub-group (`content_`).
# Ah, `TView *cbView = dialog->managedContent()->first();` this was my previous approach that I replaced.
# In `MRSetupDialogs.cpp` let's look at `runUserInterfaceSettingsDialogFlow`.
print(content.find('dialog->getData(&cbData);'))
