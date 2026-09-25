source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

import re

regex = re.compile(r"\+ 0x18c")
line_num = 1
with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
    for line in f:
        if regex.search(line) and ("Blocker" in line or "Clear" in line or "166" in str(line_num)):
            print(f"Line {line_num}: {line.strip()[:140]}")
        line_num += 1

