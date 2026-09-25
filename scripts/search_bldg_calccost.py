source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

import re

regex = re.compile(r"CBuildableBuilding.*::CalcCost")
line_num = 1
with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
    for line in f:
        if regex.search(line):
            print(f"Line {line_num}: {line.strip()}")
        line_num += 1

