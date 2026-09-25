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

# Search for 4-byte or 8-byte pointer to 0x14241875E or RVA 0x241875E
va = pe.OPTIONAL_HEADER.ImageBase + 0x241875E
va_bytes = struct.pack("<Q", va)

print(f"Searching for pointer to 0x{va:X}...")
off = 0
while True:
    idx = data.find(va_bytes, off)
    if idx == -1: break
    print(f"Found pointer in raw data at 0x{idx:X}")
    off = idx + 1
