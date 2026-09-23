with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\interface\main.gui", "r", encoding="utf-8", errors="ignore") as f:
    text = f.read()

import re
for m in re.finditer(r'name\s*=\s*"([^"]*situation[^"]*)"', text, re.IGNORECASE):
    print(m.group(0))
