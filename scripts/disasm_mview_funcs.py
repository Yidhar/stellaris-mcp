import pefile
import capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)
data = open(exe_path, "rb").read()

def rva_to_offset(rva):
    for s in pe.sections:
        va = s.VirtualAddress
        sz = s.Misc_VirtualSize
        if va <= rva < va + sz:
            return rva - va + s.PointerToRawData
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Let's disassemble CMarketView::Update (0x11C6DC0 or 0x11CA170)
for rva in [0x11C6DC0, 0x11CA170, 0x11C6AA0]:
    off = rva_to_offset(rva)
    code = data[off:off+200]
    print(f"=== Disassembly of 0x{rva:X} ===")
    for insn in md.disasm(code, rva):
        print(f"  0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
        if insn.mnemonic == 'ret': break
