import pefile
import struct
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

# Search for instructions accessing +0x158 or +0x160
# In x64: 48 8b ?? 58 01 00 00 (mov reg, [reg + 0x158])
text_sec = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
t_start = text_sec.VirtualAddress
t_end = t_start + text_sec.Misc_VirtualSize

print(f"Scanning for +0x158 accesses in .text...")
for rva in range(t_start, t_end - 7):
    off = rva_to_offset(rva)
    # Check for [reg + 0x158]: displacement is 0x158
    if data[off] == 0x48 and (data[off+1] in [0x8B, 0x89, 0x3B, 0x39, 0x83]) and data[off+3] == 0x58 and data[off+4] == 0x01 and data[off+5] == 0x00 and data[off+6] == 0x00:
        code = data[off:off+15]
        for insn in md.disasm(code, rva):
            print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
            break
