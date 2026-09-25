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

off = rva_to_offset(0x1C75440)
code = data[off:off+100]
print("=== Disassembly of 0x1C75440 ===")
for insn in md.disasm(code, 0x1C75440):
    print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
    if insn.mnemonic == 'ret': break
