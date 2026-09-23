import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import struct

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

def rva_to_offset(rva):
    for section in pe.sections:
        if section.VirtualAddress <= rva < section.VirtualAddress + section.Misc_VirtualSize:
            return rva - section.VirtualAddress + section.PointerToRawData
    return None

vt_rva = 0x258FE00
off = rva_to_offset(vt_rva)

print("--- CTechnologyView Vtable (0x258FE00) ---")
for i in range(40):
    val = struct.unpack("<Q", data[off + i*8: off + (i+1)*8])[0]
    rva = val - 0x7FF75ED50000 if val > 0x7FF75ED50000 else val
    print(f"  [{i:2d}] 0x{val:016X} (RVA 0x{rva:X})")
