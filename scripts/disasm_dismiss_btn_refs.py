import pefile, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for rva in [0x106BF00, 0x1460150]:
    off = rva_to_offset(rva)
    code = data[off : off + 0x100]
    print(f"\nDisassembly at RVA 0x{rva:X}:")
    for insn in md.disasm(code, 0x140000000 + rva):
        print(f"  0x{insn.address:X}:  {insn.mnemonic:8s} {insn.op_str}")

