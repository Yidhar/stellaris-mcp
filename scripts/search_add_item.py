source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

import re

# Find commands calling CConstructionQueue
regex = re.compile(r"CConstructionQueue::(AddItem|CanAdd)")
with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
    for line_num, line in enumerate(f, 1):
        m = regex.search(line)
        if m:
            print(f"Line {line_num}: {line.strip()[:140]}")

