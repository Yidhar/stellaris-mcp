source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

def print_lines(start, end):
    with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
        for idx, line in enumerate(f, 1):
            if start <= idx <= end:
                print(f"{idx}: {line.rstrip()}")
            elif idx > end:
                break

print_lines(1670060, 1670150)

