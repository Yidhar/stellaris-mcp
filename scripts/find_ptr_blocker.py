source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

import re

regex = re.compile(b"PTR__.*CBuildableClearDepositBlocker")
line_num = 1
found = []
with open(source_path, 'rb') as f:
    for line in f:
        if regex.search(line):
            found.append((line_num, line.decode('utf-8', errors='ignore').strip()))
        line_num += 1

print("Found PTR__...CBuildableClearDepositBlocker at:")
for ln, l in found:
    print(f"  Line {ln}: {l}")

