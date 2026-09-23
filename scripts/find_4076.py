import capstone, struct

with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb") as f:
    data = f.read()

import pefile
pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Let's search for 'cmp ..., 0x4076' in the text section
# 0x4076 in bytes: 76 40 00 00
pattern = b'\x76\x40\x00\x00'
idx = 0
found = []
while True:
    pos = data.find(pattern, idx)
    if pos == -1: break
    # check if preceded by cmp (e.g. 81 f8 76 40 00 00 or 81 fa ... or 3d 76 40 00 00)
    for back in range(1, 6):
        snippet = data[pos-back : pos+8]
        for insn in md.disasm(snippet, 0x140000000 + pos - back):
            if '4076' in insn.op_str and insn.mnemonic in ['cmp']:
                found.append((pos - back, insn.address, insn.mnemonic, insn.op_str))
    idx = pos + 1

for f in found:
    print(f"Offset 0x{f[0]:X}, VA 0x{f[1]:X}: {f[2]} {f[3]}")
    # disassemble next 25 instructions
    code = data[f[0] : f[0] + 120]
    for insn in md.disasm(code, f[1]):
        print(f"    0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")
