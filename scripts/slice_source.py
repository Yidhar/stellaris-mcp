import sys

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

if __name__ == "__main__":
    if len(sys.argv) >= 3:
        s = int(sys.argv[1])
        cnt = int(sys.argv[2])
        print_lines(s, s + cnt)
    elif len(sys.argv) == 2:
        s = int(sys.argv[1])
        print_lines(s, s + 50)
    else:
        print("Usage: slice_source.py <start_line> [count]")
