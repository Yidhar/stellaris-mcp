import pefile, struct, capstone

with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb") as f:
    data = f.read()

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)

def va_to_offset(va):
    rva = va - 0x140000000
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

def dump_descriptor(name, va):
    off = va_to_offset(va)
    print(f"\n=== Descriptor for '{name}' at VA 0x{va:X} (offset 0x{off:X}) ===")
    words = struct.unpack("<8Q", data[off:off+64])
    for i, w in enumerate(words):
        print(f"  +0x{i*8:02X}: 0x{w:016X}")

dump_descriptor("assign_leader_command (0x4076)", 0x1435B45D0)
dump_descriptor("change_leader_assignment_command (0x4292)", 0x1435DA550)
dump_descriptor("hire_leader (0x4073)", 0x1435B4270)
dump_descriptor("fire_leader_command (0x2EB3)", 0x143494530)
