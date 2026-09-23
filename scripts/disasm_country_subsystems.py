import pefile, capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_rva = text_sec.VirtualAddress

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

start_rva = 0x71E800
length = 0x600
code = data[text_start + (start_rva - text_rva) : text_start + (start_rva + length - text_rva)]

print(f"=== Disassembly of Country Ctor around 0x71E93B ===")
for insn in cs.disasm(code, start_rva):
    if insn.mnemonic == 'call' or ('lea' in insn.mnemonic and 'rcx' in insn.op_str):
        print(f"0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")
