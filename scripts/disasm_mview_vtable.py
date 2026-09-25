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

off = rva_to_offset(0x241A2D0)
funcs = struct.unpack_from("<30Q", data, off)
print("=== CMarketView Vtable (0x241A2D0) ===")
for i, f in enumerate(funcs):
    f_rva = f - pe.OPTIONAL_HEADER.ImageBase
    print(f"  [{i}] RVA 0x{f_rva:X}")
