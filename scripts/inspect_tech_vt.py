import pefile, struct

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)
with open(exe_path, "rb") as f: data = f.read()

def rva_to_off(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + s.Misc_VirtualSize:
            return rva - s.VirtualAddress + s.PointerToRawData
    return None

vt_rva = 0x2513888
off = rva_to_off(vt_rva)

print(f"--- CTechnology Vtable (RVA 0x{vt_rva:X}) ---")
for i in range(25):
    val = struct.unpack("<Q", data[off + i*8: off + (i+1)*8])[0]
    rva = val - 0x140000000 if val > 0x140000000 else val
    print(f"  [{i:2d}] 0x{val:016X} (RVA 0x{rva:X})")
