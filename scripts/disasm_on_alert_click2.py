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

off = rva_to_offset(0xA46F80)
code = data[off:off+400]
print("--- OnAlertClick part 2 (0xA46F80) ---")
for insn in md.disasm(code, 0xA46F80):
    print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
    if insn.address > 0xA47080:
        break
