source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

import re

def search_text(pattern, max_results=10):
    print(f"=== Searching for: {pattern} ===")
    regex = re.compile(pattern.encode('utf-8'))
    count = 0
    with open(source_path, 'rb') as f:
        for line_num, line in enumerate(f, 1):
            if regex.search(line):
                print(f"Line {line_num}: {line.decode('utf-8', errors='ignore').strip()[:140]}")
                count += 1
                if count >= max_results:
                    break

search_text(r"0x3ddd")
search_text(r"ClearDepositBlockerCommand")
search_text(r"CClearDepositBlocker")
search_text(r"ClearBlocker")
