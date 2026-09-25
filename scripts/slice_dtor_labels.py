source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

# In line 1669958:
# CBuildableClearDepositBlocker::~CBuildableClearDepositBlocker(CBuildableClearDepositBlocker *this)
# {
#   operator_delete(this);
#   return;
# }
# Look at nearby line numbers to see addresses in comments or labels
def print_lines(start, end):
    with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
        for idx, line in enumerate(f, 1):
            if start <= idx <= end:
                print(f"{idx}: {line.rstrip()}")
            elif idx > end:
                break

print_lines(1669940, 1669975)

