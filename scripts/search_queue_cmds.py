source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

import re

regex = re.compile(r"class\s+CAdd\w*Command")
with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
    for line_num, line in enumerate(f, 1):
        m = regex.search(line)
        if m:
            print(f"Line {line_num}: {m.group(0)}")

# Also search for other commands with "Queue" in the name
regex2 = re.compile(r"class\s+C\w*Queue\w*Command")
with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
    for line_num, line in enumerate(f, 1):
        m = regex2.search(line)
        if m:
            print(f"Line {line_num}: {m.group(0)}")

