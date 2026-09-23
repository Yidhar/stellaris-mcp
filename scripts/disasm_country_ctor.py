import pefile, capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_rva = text_sec.VirtualAddress

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

start_rva = 0x862E00
length = 0x800
code = data[text_start + (start_rva - text_rva) : text_start + (start_rva + length - text_rva)]

for insn in cs.disasm(code, start_rva):
    # Highlight lines with offsets into rbx/rdi/rcx (+0x1... or +0x2...)
    op = insn.op_str
    if any(f"+ 0x1" in op or f"+ 0x2" in op or f"+ 0x0" in op for _ in [1]):
        print(f"0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")
    elif insn.mnemonic in ['call', 'lea'] and 'rip +' in op:
        print(f"0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")
