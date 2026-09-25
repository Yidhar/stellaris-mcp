import pefile, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

target_rva = 0x1CE8800
off = rva_to_offset(target_rva)
code = data[off : off + 0x180]

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
for i in cs.disasm(code, target_rva):
    print(f"0x{i.address:X}: {i.mnemonic:8s} {i.op_str}")
