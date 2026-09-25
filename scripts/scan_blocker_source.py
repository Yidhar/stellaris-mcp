import mmap
import re

source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

def find_matches(pattern):
    print(f"=== Searching for: {pattern} ===")
    regex = re.compile(pattern.encode('utf-8'))
    line_num = 1
    with open(source_path, 'rb') as f:
        for line in f:
            if regex.search(line):
                print(f"Line {line_num}: {line.decode('utf-8', errors='ignore').strip()}")
            line_num += 1

find_matches(r"class\s+CBuildableClearDepositBlocker")
find_matches(r"CBuildableClearDepositBlocker::")
find_matches(r"ClearDepositBlocker")
