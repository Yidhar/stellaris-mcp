source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

import re

# Find all classes inheriting from CCommand or CActionCommand or ending with Command
regex = re.compile(r"class\s+C(\w*Blocker\w*|\w*Deposit\w*|\w*Clear\w*)Command")
with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
    for line_num, line in enumerate(f, 1):
        m = regex.search(line)
        if m:
            print(f"Line {line_num}: {m.group(0)}")

# Also search for "Command::Execute" with Deposit or Blocker
regex2 = re.compile(r"C\w*(?:Blocker|Deposit|Clear)\w*Command::Execute")
with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
    for line_num, line in enumerate(f, 1):
        m = regex2.search(line)
        if m:
            print(f"Line {line_num}: {m.group(0)}")

