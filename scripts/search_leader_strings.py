import re

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"

with open(exe_path, "rb") as f:
    data = f.read()

# Search for strings related to leader commands
keywords = [
    b"hire_leader",
    b"dismiss_leader",
    b"fire_leader",
    b"recruit_leader",
    b"assign_leader",
    b"leader_pool",
    b"hire_cost",
    b"leader_capacity",
    b"pool_lead",
    b"leader_trait",
    b"can_hire_leader"
]

print("Searching strings in stellaris.exe...")
for kw in keywords:
    pos = 0
    found = []
    while True:
        idx = data.find(kw, pos)
        if idx == -1:
            break
        found.append(idx)
        pos = idx + len(kw)
    print(f"Keyword '{kw.decode()}': found {len(found)} occurrences: {[hex(x) for x in found[:5]]}")

