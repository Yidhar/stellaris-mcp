import re

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"

with open(exe_path, "rb") as f:
    data = f.read()

# Search for classes named C*Command
matches = set()
for m in re.finditer(rb'C[A-Z][a-zA-Z0-9_]+Command', data):
    cmd_name = m.group(0).decode('latin-1')
    matches.add(cmd_name)

print(f"Found {len(matches)} C*Command names:")
for name in sorted(matches):
    if any(k in name.lower() for k in ['tech', 'res', 'sci', 'select', 'card']):
        print(f"  * {name}")
    elif len(matches) < 50:
        print(f"    {name}")
