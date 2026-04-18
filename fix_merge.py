import sys
import os

with open('ui/TMREditWindow.hpp', 'r') as f:
    content = f.read()

import re
content = re.sub(r'<<<<<<< HEAD.*?=======(.*?)>>>>>>> origin/main', r'\1', content, flags=re.DOTALL)

with open('ui/TMREditWindow.hpp', 'w') as f:
    f.write(content)
