import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

def rva_to_offset(rva):
    for section in pe.sections:
        if section.VirtualAddress <= rva < section.VirtualAddress + section.Misc_VirtualSize:
            return rva - section.VirtualAddress + section.PointerToRawData
    return None

with open(exe_path, "rb") as f:
    data = f.read()

md = Cs(CS_ARCH_X86, CS_MODE_64)

start_rva = 0x1061DD0
off = rva_to_offset(start_rva)
code = data[off:off+300]
print(f"--- Disassembly from 0x{start_rva:X} ---")
for insn in md.disasm(code, start_rva):
    print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
    if insn.address > 0x1061EC0:
        break
