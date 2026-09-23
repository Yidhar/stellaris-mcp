import pefile, struct, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

def offset_to_rva(offset):
    for sec in pe.sections:
        if sec.PointerToRawData <= offset < sec.PointerToRawData + sec.SizeOfRawData:
            return sec.VirtualAddress + (offset - sec.PointerToRawData)
    return None

# Let's inspect the command registration object of dismiss_leader:
# RVA 0x35B4390 (offset: RVA - 0xC00)
# Remember in inspect_cmd_objects.py:
# +0x0: 0x4074
# +0x8: 0x00007FF7611EE1D8 (Live RVA 0x249E1D8)
# +0x10: 0x00007FF7623043B0
# +0x18: 0x0000000F00000100
# +0x20: 'dismiss_leader'
# Let's see what is at RVA 0x249E1D8!
off = rva_to_offset(0x249E1D8)
print(f"RVA 0x249E1D8 (offset 0x{off:X}):")
for i in range(10):
    va = struct.unpack('<Q', data[off + i*8 : off + (i+1)*8])[0]
    print(f"  [{i}] 0x{va:X} (RVA 0x{va - 0x140000000:X})")

