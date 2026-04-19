import re

with open("app/MRAppState.cpp", "r") as f:
    content = f.read()

target = "setCommandEnabled(cmMrBlockPersistent, hasEditor);"
replacement = "setCommandEnabled(cmMrBlockPersistent, hasEditor);\n\tsetCommandEnabled(cmMrWindowOrganizeCascade, hasEditor);\n\tstd::size_t numWindows = allEditWindowsInZOrder().size();\n\tsetCommandEnabled(cmMrWindowOrganizeTile, hasEditor && numWindows < 10);\n\tsetCommandEnabled(cmMrWindowOrganizeWindowManager, true);"

if "cmMrWindowOrganizeCascade" not in content:
    content = content.replace(target, replacement)

    with open("app/MRAppState.cpp", "w") as f:
        f.write(content)
