import pefile, struct

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

# Notice 0x35B4270 -> 0x35B4390 is 0x120 bytes!
# 0x35B4390 -> 0x35B45D0 is 0x240 bytes (two 0x120 items!)
# Each command descriptor is 0x120 bytes (288 bytes)!
off = rva_to_offset(0x35B4390)
print(f"Command descriptor for dismiss_leader at offset 0x{off:X}:")
for i in range(0, 0x120, 8):
    q = struct.unpack('<Q', data[off + i : off + i + 8])[0]
    u0 = struct.unpack('<I', data[off + i : off + i + 4])[0]
    u1 = struct.unpack('<I', data[off + i + 4 : off + i + 8])[0]
    extra = ""
    if 0x140000000 <= q <= 0x143000000:
        extra = f" -> Code/Rdata RVA 0x{q - 0x140000000:X}"
    print(f"  +{hex(i)}: 0x{q:016X} | u0={u0}, u1={u1}{extra}")

