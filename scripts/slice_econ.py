source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

def print_lines(start, end):
    print(f"\n=== Lines {start} to {end} ===")
    line_num = 1
    with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            if start <= line_num <= end:
                print(f"{line_num}: {line.rstrip()}")
            elif line_num > end:
                break
            line_num += 1

print_lines(1660870, 1660975)
