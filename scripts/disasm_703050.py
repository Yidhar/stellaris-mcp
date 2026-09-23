import pefile, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

off = rva_to_offset(0x703050)
code = data[off : off + 0x100]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
print("Disassembly of 0x703050:")
for ins in md.disasm(code, 0x140703050):
    print(f"  0x{ins.address:X}:  {ins.mnemonic:8s} {ins.op_str}")

