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

rva = 0x2390F30
off = rva_to_offset(rva)
col_ptr = struct.unpack_from("<Q", data, off - 8)[0]
col_rva = col_ptr - pe.OPTIONAL_HEADER.ImageBase
col_off = rva_to_offset(col_rva)
print(f"col_ptr: 0x{col_ptr:X}, col_rva: 0x{col_rva:X}, col_off: 0x{col_off:X}")
print("col raw bytes:", data[col_off:col_off+32].hex(' '))
td_rva = struct.unpack_from("<I", data, col_off + 12)[0]
print(f"td_rva: 0x{td_rva:X}")

