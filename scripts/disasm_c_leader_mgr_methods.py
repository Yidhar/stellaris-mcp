import pefile, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for rva, name in [
    (0x847E00, "vfunc[18]"),
    (0x847F30, "vfunc[19]"),
    (0x847000, "vfunc[21]"),
    (0x8471A0, "vfunc[23]"),
    (0x847850, "vfunc[14]"),
    (0x847CF0, "vfunc[10]"),
    (0x847D00, "vfunc[13]"),
]:
    off = rva_to_offset(rva)
    code = data[off : off + 0x60]
    print(f"\n--- {name} (RVA 0x{rva:X}) ---")
    for ins in md.disasm(code, 0x140000000 + rva):
        print(f"  0x{ins.address:X}:  {ins.mnemonic:8s} {ins.op_str}")

