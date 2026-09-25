source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

import re

regex = re.compile(r"^\/\*\s*(CPlanetViewDepositEntry::\w+.*?)\s*\*\/")
methods = []
with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
    for line_num, line in enumerate(f, 1):
        if 4666000 <= line_num <= 4668000:
            m = regex.match(line)
            if m:
                methods.append((line_num, m.group(1)))
        elif line_num > 4668000:
            break

print(f"Total CPlanetViewDepositEntry methods found: {len(methods)}")
for ln, m in methods:
    print(f"  Line {ln}: {m}")

