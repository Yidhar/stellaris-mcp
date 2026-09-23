import pefile
import struct

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

def get_class_name(vtable_rva):
    vt_off = rva_to_offset(vtable_rva)
    if not vt_off or vt_off < 8:
        return "No offset"
    col_ptr = struct.unpack('<Q', data[vt_off-8:vt_off])[0]
    col_rva = col_ptr - 0x140000000
    col_off = rva_to_offset(col_rva)
    if not col_off or col_off + 24 > len(data):
        return f"COL invalid (ptr=0x{col_ptr:X}, col_rva=0x{col_rva:X})"
    
    td_rva = struct.unpack('<I', data[col_off+12:col_off+16])[0]
    td_off = rva_to_offset(td_rva)
    if not td_off:
        return f"TD RVA 0x{td_rva:X} invalid"
    
    raw_name = data[td_off+16:td_off+128].split(b'\x00')[0]
    return raw_name.decode('latin-1', errors='ignore')

base = 0x7ff75ed50000
test_ptrs = [
    0x7FF76128E168,
    0x7FF761788770,
    0x7FF761262a38,
    0x7FF76125e600,
    0x7FF76178e540,
    0x7FF7612acd78,
    0x7FF7611ed690
]

for p in test_ptrs:
    rva = p - base
    print(f"Ptr {hex(p)} (RVA 0x{rva:X}): {get_class_name(rva)}")

print("\n--- Searching for *Leader* in all TypeDescriptors ---")
import re
# Find all occurrences of b".?AV.*Leader.*@@"
pattern = re.compile(rb'\.\?AV[A-Za-z0-9_]*Leader[A-Za-z0-9_]*@@')
matches = set(pattern.findall(data))
for m in sorted(matches):
    print("Found class:", m.decode('latin-1'))

