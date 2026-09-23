import pefile, struct, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

off = rva_to_offset(0x2538B70)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

print("Vtable CCountryLeaderManager at RVA 0x2538B70:")
for i in range(25):
    va = struct.unpack('<Q', data[off + i*8 : off + (i+1)*8])[0]
    rva = va - 0x140000000
    print(f"  vfunc[{i:2d}]: VA 0x{va:X} (RVA 0x{rva:X})")

