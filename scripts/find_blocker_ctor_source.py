source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

import re

regex = re.compile(b"CBuildableClearDepositBlocker::CBuildableClearDepositBlocker")
line_num = 1
with open(source_path, 'rb') as f:
    for line in f:
        if regex.search(line):
            print(f"Line {line_num}: {line.decode('utf-8', errors='ignore').strip()}")
        line_num += 1

