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

for rva in [0x1061DB0, 0x1062970, 0x1062A30, 0x10636B0, 0x105BE90, 0x105BAE0]:
    off = rva_to_offset(rva)
    code = data[off:off+40]
    print(f"\n--- Method RVA 0x{rva:X} ---")
    for insn in md.disasm(code, rva):
        print(f"  0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
        if insn.mnemonic in ['ret', 'jmp']:
            break
