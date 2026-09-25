source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

import re

def list_methods(start, end):
    print(f"=== Methods between {start} and {end} ===")
    regex = re.compile(r"^\/\*\s*(.*?)\s*\*\/")
    with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
        for line_num, line in enumerate(f, 1):
            if start <= line_num <= end:
                m = regex.match(line)
                if m:
                    print(f"Line {line_num}: {m.group(1)}")
            elif line_num > end:
                break

list_methods(1660600, 1661250)
