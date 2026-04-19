import re

with open("app/MRCommandRouter.cpp", "r") as f:
    content = f.read()

target = """		case cmMrWindowAdjacent:
			return mrHandleAdjacentWindows();"""

replacement = """		case cmMrWindowAdjacent:
			return mrHandleAdjacentWindows();
		case cmMrWindowOrganizeCascade:
			return mrHandleCascadeWindows();
		case cmMrWindowOrganizeTile:
			return mrHandleTileWindows();"""

if "case cmMrWindowOrganizeCascade:" not in content:
    content = content.replace(target, replacement)
    with open("app/MRCommandRouter.cpp", "w") as f:
        f.write(content)
