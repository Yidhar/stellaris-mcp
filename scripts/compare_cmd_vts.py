import pefile, struct

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

def print_vt(name, rva):
    off = rva_to_offset(rva)
    print(f"\n=== Vtable '{name}' at RVA 0x{rva:X} (offset 0x{off:X}) ===")
    for i in range(12):
        q = struct.unpack('<Q', data[off + i*8 : off + (i+1)*8])[0]
        q_rva = q - 0x140000000
        print(f"  vfunc[{i:2d}]: 0x{q:016X} (RVA 0x{q_rva:X})")

print_vt("CFinishAgendaCommand", 0x2530AD8)
print_vt("CAddEdictCommand", 0x25302B8)
print_vt("CRemoveEdictCommand", 0x2530370)
print_vt("CHireLeaderCommand candidate", 0x2530F80)

