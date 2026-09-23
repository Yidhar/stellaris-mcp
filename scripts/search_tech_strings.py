import pefile
import re

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"

with open(exe_path, "rb") as f:
    data = f.read()

def find_strings(pattern):
    matches = []
    regex = re.compile(pattern.encode('utf-8'))
    for m in regex.finditer(data):
        matches.append(m.start())
    return matches

print("Searching for CSelectTech / CResearchTech / CTechManager...")
for p in [r"CSelectTechCommand", r"CResearchTech", r"CTechManager", r"SelectTech", r"select_tech", r"technology_view"]:
    res = find_strings(p)
    print(f"Pattern '{p}': {len(res)} matches")
    for off in res[:5]:
        # print surrounding text
        s = data[max(0, off-20):min(len(data), off+50)]
        print(f"  Offset 0x{off:X}: {s}")
