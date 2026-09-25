import pefile
import capstone
import struct

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

# In CMarketBuyResourceCommand:
# [0] 0x6BE620: Destructor?
# [1] 0x1BAA8F0: Clone?
# [2] 0x1A7B530: GetToken? (0x33c0?)
# Let's check CMarketBuyResourceCommand [2] vs [4]
for rva in [0x6BE620, 0x1BAA8F0, 0x1A7B530, 0x50D4C0, 0x1A7B640]:
    off = rva_to_offset(rva)
    code = data[off:off+40]
    print(f"--- 0x{rva:X} ---")
    for insn in md.disasm(code, rva):
        print(f"  0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
        if insn.mnemonic == 'ret': break

print("\n=== Now CAddMonthlyTradeCommand ===")
# [0] 0x1D10810
# [1] 0x1D11010
# [2] 0x11B72B0 (mov eax, 0x33c7; ret) -> GetToken!
# [3] 0x155C00
# [4] 0x11B71F0
for rva in [0x1D10810, 0x1D11010, 0x11B72B0, 0x155C00, 0x11B71F0]:
    off = rva_to_offset(rva)
    code = data[off:off+50]
    print(f"--- 0x{rva:X} ---")
    for insn in md.disasm(code, rva):
        print(f"  0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
        if insn.mnemonic == 'ret': break
