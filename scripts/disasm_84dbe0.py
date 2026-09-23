import pefile, capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)
with open(exe_path, "rb") as f: data = f.read()

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_rva = text_sec.VirtualAddress

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

start_rva = 0x84DBE0
code = data[text_start + (start_rva - text_rva) : text_start + (start_rva + 0xA0 - text_rva)]

for insn in cs.disasm(code, start_rva):
    print(f"0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")
