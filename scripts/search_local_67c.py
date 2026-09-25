source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

import re

regex = re.compile(r"local_67c")
line_num = 1
with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
    for line in f:
        if 3483400 <= line_num <= 3483750:
            if regex.search(line):
                print(f"Line {line_num}: {line.strip()}")
        elif line_num > 3483750:
            break
        line_num += 1

