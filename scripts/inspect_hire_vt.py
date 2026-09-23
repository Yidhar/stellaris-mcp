import pefile
import struct

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

def offset_to_rva(offset):
    for sec in pe.sections:
        if sec.PointerToRawData <= offset < sec.PointerToRawData + sec.SizeOfRawData:
            return sec.VirtualAddress + (offset - sec.PointerToRawData)
    return None

target = 0x140714490
p = struct.pack('<Q', target)

pos = 0
found = []
while True:
    idx = data.find(p, pos)
    if idx == -1:
        break
    found.append(idx)
    pos = idx + 1

print(f"Found {len(found)} references to 0x140714490:")
for off in found:
    rva = offset_to_rva(off)
    va = 0x140000000 + rva
    print(f"  Offset 0x{off:X}, RVA 0x{rva:X}, VA 0x{va:X}")
    
    # In Stellaris, GetType() is vfunc[2] or vfunc[3]
    # Let's inspect the 5 qwords before and 10 qwords after
    start_off = off - 32
    print("  Vtable entries:")
    for i in range(12):
        q = struct.unpack('<Q', data[start_off + i*8 : start_off + (i+1)*8])[0]
        q_rva = q - 0x140000000
        cur_off = start_off + i*8
        marker = " <=== GetType (0x4073)" if cur_off == off else ""
        print(f"    [{i-4:2d}] Offset 0x{cur_off:X} (RVA 0x{offset_to_rva(cur_off):X}): 0x{q:016X} (fn RVA 0x{q_rva:X}){marker}")

