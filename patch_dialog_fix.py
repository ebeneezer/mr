import re

with open("dialogs/MRSetupDialogs.cpp", "r") as f:
    content = f.read()

# Replace execDialog with execDialogWithDataCapture
target = """		TUserInterfaceSettingsDialog *dialog = new TUserInterfaceSettingsDialog(currentWm, currentMm);
		ushort result = execDialog(dialog);

		if (result == cmOK) {
			ushort cbData = 0;
			if (dialog != nullptr) {
				dialog->getData(&cbData);
			}"""

replacement = """		TUserInterfaceSettingsDialog *dialog = new TUserInterfaceSettingsDialog(currentWm, currentMm);
		ushort cbData = 0;
		if (currentWm)
			cbData |= 1;
		if (currentMm)
			cbData |= 2;

		ushort result = execDialogWithDataCapture(dialog, &cbData);

		if (result == cmOK) {"""

content = content.replace(target, replacement)

# We also need to remove the internal setData in constructor because execDialogWithDataCapture does it.
target_ctor = """		ushort cbData = 0;
		if (initialWindowManager)
			cbData |= 1;
		if (initialMenulineMessages)
			cbData |= 2;
		cb->setData(&cbData);
		addManaged(cb, cb->getBounds());"""

replacement_ctor = """		addManaged(cb, cb->getBounds());"""
content = content.replace(target_ctor, replacement_ctor)

with open("dialogs/MRSetupDialogs.cpp", "w") as f:
    f.write(content)
