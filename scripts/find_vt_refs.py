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

vt_rva = 0x2418C58

# In x64 code, references to 0x2418C58 are typically `lea rax, [rip + disp]`
# Search in .text section
text_sec = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
text_start = text_sec.VirtualAddress
text_end = text_start + text_sec.Misc_VirtualSize
text_off = text_sec.PointerToRawData

print(f"Scanning .text (0x{text_start:X} - 0x{text_end:X})...")
for rva in range(text_start, text_end - 7):
    # check for 48 8d ?? ?? ?? ?? ??
    # offset in data
    off = rva_to_offset(rva)
    b0 = data[off]
    b1 = data[off+1]
    b2 = data[off+2]
    if b0 == 0x48 and b1 == 0x8D: # lea reg, [rip + disp32]
        disp = struct.unpack_from("<i", data, off + 3)[0]
        target = rva + 7 + disp
        if target == vt_rva:
            print(f"Found reference at RVA 0x{rva:X}")
