import pefile, capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_rva = text_sec.VirtualAddress

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for target_rva in [0x1BF1630, 0x1BF1640, 0x1BF1660, 0x1BF1730, 0x1BF18A0, 0x1BF18C0, 0x1BF18D0, 0x1BF1B70, 0x1BF1BB0]:
    start_off = text_start + (target_rva - text_rva)
    code = data[start_off:start_off + 0x50]
    print(f"\n--- Method at 0x{target_rva:X} ---")
    count = 0
    for insn in cs.disasm(code, target_rva):
        print(f"  0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
        count += 1
        if count >= 12 or insn.mnemonic == 'ret':
            break
