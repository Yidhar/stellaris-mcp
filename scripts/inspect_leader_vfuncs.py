import pefile, struct, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

off = rva_to_offset(0x253E168)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

print("CLeader vtable at RVA 0x253E168:")
for i in range(16):
    va = struct.unpack('<Q', data[off + i*8 : off + (i+1)*8])[0]
    rva = va - 0x140000000
    print(f"  vfunc[{i:2d}]: VA 0x{va:X} (RVA 0x{rva:X})")
    # Disassemble 16 bytes of each
    fn_off = rva_to_offset(rva)
    if fn_off:
        for ins in md.disasm(data[fn_off:fn_off+24], va):
            print(f"      {ins.mnemonic} {ins.op_str}")

