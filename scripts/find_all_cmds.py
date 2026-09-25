source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

import re

# In Ghidra: /* C...Command::Execute() */ or /* C...Command::ExecuteLocal() */
regex = re.compile(r"^\/\*\s*(C\w+Command)::ExecuteLocal\(\)")
cmds = set()
with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
    for line_num, line in enumerate(f, 1):
        m = regex.match(line)
        if m:
            cmds.add(m.group(1))

print(f"Total commands found: {len(cmds)}")
for c in sorted(cmds):
    if any(k in c.lower() for k in ["queue", "build", "deposit", "planet", "colony", "blocker", "add"]):
        print(f"  {c}")

