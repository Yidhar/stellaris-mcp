import pefile, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def va_to_offset(va):
    rva = va - 0x140000000
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

refs = [0x14032cac4, 0x140f4d274, 0x140f4d35b, 0x141087c1f, 0x14108dac9, 0x1410c557e]

for va in refs:
    off = va_to_offset(va - 0x20)
    print(f"=== Disasm around 0x{va:X} ===")
    for insn in md.disasm(data[off : off + 0x60], va - 0x20):
        print(f"  0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")
