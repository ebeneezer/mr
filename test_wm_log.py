import re

with open("ui/MRWindowManager.cpp", "r") as f:
    content = f.read()

target = 'if ((mode & dmDragMove) == 0) {\n        return;\n    }'
replacement = """if ((mode & dmDragMove) == 0) {
        return;
    }

    TPoint mouseLocal = window->owner->makeLocal(event.mouse.where);
    TRect deskExtent = window->owner->getExtent(); // Desktop extent

    mrLogMessage("MRWindowManager: drag finished. global mouse=(" + std::to_string(event.mouse.where.x) + "," + std::to_string(event.mouse.where.y) + ") " +
                 "local mouse=(" + std::to_string(mouseLocal.x) + "," + std::to_string(mouseLocal.y) + ") " +
                 "deskExtent=(" + std::to_string(deskExtent.a.x) + "," + std::to_string(deskExtent.a.y) + " to " + std::to_string(deskExtent.b.x) + "," + std::to_string(deskExtent.b.y) + ")");
"""
content = content.replace(target, replacement)

# Add #include "MRWindowSupport.hpp"
if "MRWindowSupport.hpp" not in content:
    content = '#include "MRWindowSupport.hpp"\n#include <string>\n' + content

with open("ui/MRWindowManager.cpp", "w") as f:
    f.write(content)
