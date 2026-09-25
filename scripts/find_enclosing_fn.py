source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

# Search backwards from 3483528 for function start /* ... */
import re
regex = re.compile(r"^\/\*\s*(.*?)\s*\*\/")
last_fn = None
with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
    for line_num, line in enumerate(f, 1):
        if line_num > 3483530:
            break
        m = regex.match(line)
        if m:
            last_fn = (line_num, m.group(1))

print(f"Enclosing function: Line {last_fn[0]}: {last_fn[1]}")

