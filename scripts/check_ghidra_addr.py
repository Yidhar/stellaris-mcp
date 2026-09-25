source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

# In line 4666397:
# *local_60 = (long)&PTR__CBuildableBase_0530cc68;
# In line 4666398:
# /* try { // try from 0318b098 to 0318b0ac has its CatchHandler @ 0318b11e */
# Notice the address: 0318b098!
# Let's search for bytes around 0318b098 in the Ghidra listing, or disassemble around 0x1200000 to find 0318b098!
# Wait! In Ghidra, the base address is 0x0140000000 or 0x00400000 or 0x02000000?
# Notice 0318b098:
# If image base was 0x140000000, 0x140000000 + 0x1200000? No!
# Let's search for "0318b098" in source!

def print_lines(start, end):
    with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
        for idx, line in enumerate(f, 1):
            if start <= idx <= end:
                print(f"{idx}: {line.rstrip()}")
            elif idx > end:
                break

print_lines(4666390, 4666405)

