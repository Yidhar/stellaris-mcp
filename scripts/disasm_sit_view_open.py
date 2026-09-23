import pefile, capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_rva = text_sec.VirtualAddress

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

target_rva = 0x1BF1660
start_off = text_start + (target_rva - text_rva)
code = data[start_off:start_off + 0x180]

print(f"=== Disassembly of CSituationLogView::Open (0x{target_rva:X}) ===")
for insn in cs.disasm(code, target_rva):
    print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
