import pefile, struct

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)
with open(exe_path, "rb") as f: data = f.read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

off = rva_to_offset(0x2539110)
print(f"Around 0x2539110 (off=0x{off:X}):")
for i in range(-10, 10):
    val = struct.unpack('<Q', data[off + i*8 : off + (i+1)*8])[0]
    rva = val - 0x140000000 if val > 0x140000000 else 0
    print(f"  [{i:+02d}] 0x{val:X} (RVA 0x{rva:X})")
