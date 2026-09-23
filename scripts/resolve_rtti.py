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
    # COL pointer is at vt_off - 8
    col_ptr = struct.unpack('<Q', data[vt_off-8:vt_off])[0]
    # In 64-bit MSVC, COL contains:
    # 0x00: signature (0 or 1)
    # 0x04: offset
    # 0x08: cdOffset
    # 0x0C: pTypeDescriptor (RVA, relative to image base!)
    # 0x10: pClassDescriptor (RVA)
    # 0x14: pSelf (RVA of COL itself)
    col_rva = col_ptr - 0x140000000
    col_off = rva_to_offset(col_rva)
    if not col_off or col_off + 24 > len(data):
        return f"COL invalid (ptr=0x{col_ptr:X}, col_rva=0x{col_rva:X})"
    
    td_rva = struct.unpack('<I', data[col_off+12:col_off+16])[0]
    td_off = rva_to_offset(td_rva)
    if not td_off:
        return f"TD RVA 0x{td_rva:X} invalid"
    
    # TypeDescriptor:
    # 0x00: pVFTable
    # 0x08: spare
    # 0x10: name (null-terminated string, e.g. .?AVClassName@@)
    raw_name = data[td_off+16:td_off+128].split(b'\x00')[0]
    return raw_name.decode('latin-1', errors='ignore')

test_rvas = [
    0x258E5E8,
    0x25DC7F8, 0x25DC838, 0x25DC9C8, 0x25DCA68, 0x25DCB98, 0x25DCC38, 0x25DCEE0, 0x25DCF20, 0x25DD038,
    0x25D38C8, 0x25D1590, 0x25B66C0, 0x25D1AA0, 0x2592BE8, 0x25D05D0, 0x258A108, 0x25B6ED0
]

for rva in test_rvas:
    print(f"0x{rva:X}: {get_class_name(rva)}")
