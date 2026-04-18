import re

with open("app/MRAppState.cpp", "r") as f:
    content = f.read()

target = """	setCommandEnabled(cmMrWindowOrganizeCascade, hasEditor);
	std::size_t numWindows = allEditWindowsInZOrder().size();
	setCommandEnabled(cmMrWindowOrganizeTile, hasEditor && numWindows < 10);
	setCommandEnabled(cmMrWindowOrganizeWindowManager, true);"""

content = content.replace(target, "", 1) # remove the duplicate insert

with open("app/MRAppState.cpp", "w") as f:
    f.write(content)
