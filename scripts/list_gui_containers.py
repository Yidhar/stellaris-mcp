import re

with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\interface\situation_log.gui", "r", encoding="utf-8", errors="ignore") as f:
    text = f.read()

for m in re.finditer(r'(containerWindowType|smoothListboxType|gridBoxType|treeBoxType)\s*=\s*\{[^}]*?name\s*=\s*"([^"]+)"', text, re.DOTALL):
    print(f"{m.group(1)}: {m.group(2)}")
