import pefile

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

def rva_to_offset(rva):
    for section in pe.sections:
        if section.VirtualAddress <= rva < section.VirtualAddress + section.Misc_VirtualSize:
            return rva - section.VirtualAddress + section.PointerToRawData
    return None

off = rva_to_offset(0x106072F)

# Scan backwards for function prologue: 0x55 0x48 0x89 0xE5 or 0x48 0x89 0x5C 0x24 or 0x48 0x83 0xEC
for i in range(off, off - 0x2000, -1):
    # Check for CC int3 before prologue
    if data[i-1] == 0xCC and data[i] != 0xCC:
        rva = 0x106072F - (off - i)
        print(f"Function start candidate: 0x{rva:X}")
        break
