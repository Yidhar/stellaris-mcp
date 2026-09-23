try:
    import capstone
    has_capstone = True
except ImportError:
    has_capstone = False

with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb") as f:
    data = f.read()

start_off = 0x1A10F0
length = 0x200
code = data[start_off : start_off + length]

if has_capstone:
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for insn in md.disasm(code, 0x140000000 + 0x1A1CF0):
        print(f"0x{insn.address:X}:  {insn.mnemonic:8s} {insn.op_str}")
else:
    print("No capstone, dumping hex:")
    print(code.hex())

