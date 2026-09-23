import pefile, struct

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

vt_rva = 0x258E5E8
off = rva_to_offset(vt_rva)
print(f"CSituationLogView Vtable at 0x{vt_rva:X}:")
for i in range(25):
    fn_ptr = struct.unpack('<Q', data[off + i*8 : off + (i+1)*8])[0]
    print(f"  vfunc[{i:02d}]: 0x{fn_ptr - 0x140000000:X}")
