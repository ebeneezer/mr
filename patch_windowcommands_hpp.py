import re

with open("app/commands/MRWindowCommands.hpp", "r") as f:
    content = f.read()

target = "bool mrHandleAdjacentWindows();"
replacement = "bool mrHandleAdjacentWindows();\nbool mrHandleCascadeWindows();\nbool mrHandleTileWindows();"

content = content.replace(target, replacement)

with open("app/commands/MRWindowCommands.hpp", "w") as f:
    f.write(content)
