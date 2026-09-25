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

# Find "resources_grid"
str_off = data.find(b"resources_grid\0")
print(f"resources_grid off: 0x{str_off:X}")
# convert to rva
for s in pe.sections:
    if s.PointerToRawData <= str_off < s.PointerToRawData + s.SizeOfRawData:
        str_rva = str_off - s.PointerToRawData + s.VirtualAddress
        print(f"resources_grid RVA: 0x{str_rva:X}")
        break

# Find references to str_rva in .text
text_sec = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
t_start = text_sec.VirtualAddress
t_end = t_start + text_sec.Misc_VirtualSize

for rva in range(t_start, t_end - 7):
    off = rva_to_offset(rva)
    if data[off] == 0x48 and data[off+1] == 0x8D:
        disp = struct.unpack_from("<i", data, off + 3)[0]
        target = rva + 7 + disp
        if target == str_rva:
            print(f"Found LEA to resources_grid at RVA 0x{rva:X}")
