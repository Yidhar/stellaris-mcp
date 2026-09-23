import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import struct

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

target_rva = 0x258F808
text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_end = text_start + text_sec.SizeOfRawData
text_data = data[text_start:text_end]

for i in range(len(text_data) - 7):
    # Check 48 8D or 4C 8D
    if (text_data[i] in [0x48, 0x4C]) and text_data[i+1] == 0x8D:
        disp = struct.unpack("<i", text_data[i+3:i+7])[0]
        insn_rva = text_sec.VirtualAddress + i
        if insn_rva + 7 + disp == target_rva:
            print(f"Found LEA at RVA 0x{insn_rva:X}")
    # Also check mov reg, [rip + disp]
    if (text_data[i] in [0x48, 0x4C]) and text_data[i+1] == 0x8B:
        disp = struct.unpack("<i", text_data[i+3:i+7])[0]
        insn_rva = text_sec.VirtualAddress + i
        if insn_rva + 7 + disp == target_rva:
            print(f"Found MOV at RVA 0x{insn_rva:X}")
