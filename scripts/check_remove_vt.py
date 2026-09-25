import pefile
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

off = rva_to_offset(0x2418BA0)
funcs = struct.unpack_from("<10Q", data, off)
print("=== Vtable at 0x2418BA0 (CRemoveMonthlyTradeCommand) ===")
for i, f in enumerate(funcs):
    f_rva = f - pe.OPTIONAL_HEADER.ImageBase
    print(f"  [{i}] RVA 0x{f_rva:X}")
