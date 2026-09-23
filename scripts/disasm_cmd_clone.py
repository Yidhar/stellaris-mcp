import pefile, capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_rva = text_sec.VirtualAddress

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for rva in [0x710CE0, 0x7125A0]:
    start_off = text_start + (rva - text_rva)
    code = data[start_off:start_off + 0xC0]
    print(f"\n=== Disassembly of 0x{rva:X} ===")
    for insn in cs.disasm(code, rva):
        print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
