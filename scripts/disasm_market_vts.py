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

import struct

# Vtables:
# CAddMonthlyTradeCommand: 0x2418C98
# CRemoveMonthlyTradeCommand: 0x2418BE0
# CUpdateMonthlyTradeCommand: 0x2418B28
# CMarketBuyResourceCommand: 0x245BD60
# CMarketSellResourceCommand: 0x23C9128

for name, rva in [
    ("CMarketBuyResourceCommand", 0x245BD60),
    ("CAddMonthlyTradeCommand", 0x2418C98),
    ("CRemoveMonthlyTradeCommand", 0x2418BE0),
]:
    off = rva_to_offset(rva)
    print(f"=== {name} (RVA 0x{rva:X}) ===")
    # Print first 10 vtable entries
    funcs = struct.unpack_from("<10Q", data, off)
    for i, f in enumerate(funcs):
        # f is an absolute VA with imagebase 0x7FF7xxxx0000 or relative?
        # In PE file on disk, .rdata entries are relocatable absolute addresses (ImageBase 0x140000000 or similar)
        f_rva = f - pe.OPTIONAL_HEADER.ImageBase
        print(f"  [{i}] RVA 0x{f_rva:X} (Raw: 0x{f:X})")
