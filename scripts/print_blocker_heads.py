source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

def print_fn_head(line_no):
    with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
        for idx, line in enumerate(f, 1):
            if line_no - 3 <= idx <= line_no + 10:
                print(f"{idx}: {line.rstrip()}")
            elif idx > line_no + 10:
                break

for l in [1660627, 1660641, 1660668, 1660795, 1660807, 1660819, 1660831, 1660910, 1660988, 1661030, 1661066, 1661094, 1661157]:
    print("\n-------------------------")
    print_fn_head(l)

