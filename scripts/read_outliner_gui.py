path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\interface\outliner.gui"
with open(path, "r", encoding="utf-8-sig") as f:
    lines = f.readlines()

print(f"Total lines in outliner.gui: {len(lines)}")
# Print out container/window names in outliner.gui
for i, line in enumerate(lines):
    if "name = " in line:
        print(f"L{i+1}: {line.strip()}")
