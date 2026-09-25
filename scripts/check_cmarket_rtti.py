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

off = rva_to_offset(0x23396E8)
funcs = struct.unpack_from("<10Q", data, off)
print("=== 0x23396E8 Vtable ===")
for i, f in enumerate(funcs):
    f_rva = f - pe.OPTIONAL_HEADER.ImageBase
    print(f"  [{i}] RVA 0x{f_rva:X}")

# Check RTTI complete object locator at off - 8
col_ptr = struct.unpack_from("<Q", data, off - 8)[0]
col_rva = col_ptr - pe.OPTIONAL_HEADER.ImageBase
col_off = rva_to_offset(col_rva)
td_rva = struct.unpack_from("<I", data, col_off + 12)[0]
td_off = rva_to_offset(td_rva)
name = data[td_off+16:td_off+60].split(b'\0')[0].decode('ascii', errors='ignore')
print(f"Type: {name}")
