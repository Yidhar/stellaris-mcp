import capstone

with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb") as f:
    data = f.read()

# RVA 0x1D36B40
# File offset: let's calculate from pefile
import pefile
pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

rva = 0x1D36B40
off = rva_to_offset(rva)
print(f"RVA 0x{rva:X} -> offset 0x{off:X}")

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
code = data[off : off + 0x80]
for insn in md.disasm(code, 0x140000000 + rva):
    print(f"0x{insn.address:X}:  {insn.mnemonic:8s} {insn.op_str}")

