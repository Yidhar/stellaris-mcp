import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import struct

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

def offset_to_rva(off):
    for section in pe.sections:
        if section.PointerToRawData <= off < section.PointerToRawData + section.SizeOfRawData:
            return off - section.PointerToRawData + section.VirtualAddress
    return None

target_rva = 0x10616D0

text_sec = None
for s in pe.sections:
    if s.Name.startswith(b'.text'):
        text_sec = s
        break

text_start = text_sec.PointerToRawData
text_end = text_start + text_sec.SizeOfRawData
text_data = data[text_start:text_end]

print(f"Searching for CALLs to 0x{target_rva:X}...")
for i in range(len(text_data) - 5):
    # E8 rel32
    if text_data[i] == 0xE8:
        rel = struct.unpack("<i", text_data[i+1:i+5])[0]
        insn_rva = text_sec.VirtualAddress + i
        dest = insn_rva + 5 + rel
        if dest == target_rva:
            print(f"Found CALL at RVA 0x{insn_rva:X}")
