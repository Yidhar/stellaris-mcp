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

def offset_to_rva(off):
    for section in pe.sections:
        if section.PointerToRawData <= off < section.PointerToRawData + section.SizeOfRawData:
            return off - section.PointerToRawData + section.VirtualAddress
    return None

# Find "technology_view_window" offset
target_str = b"technology_view_window\x00"
str_off = data.find(target_str)
str_rva = offset_to_rva(str_off)
print(f"'technology_view_window' string RVA: 0x{str_rva:X}")

# Find references to this string in .text
text_sec = None
for s in pe.sections:
    if s.Name.startswith(b'.text'):
        text_sec = s
        break

text_start = text_sec.PointerToRawData
text_end = text_start + text_sec.SizeOfRawData
text_data = data[text_start:text_end]

md = Cs(CS_ARCH_X86, CS_MODE_64)

# RIP-relative LEA search: target_rva = insn.address + insn.size + disp
print("Searching for RIP-relative references in .text...")
for i in range(len(text_data) - 7):
    # LEA reg, [rip + disp32]: 48 8D ?? disp32
    if text_data[i] == 0x48 and text_data[i+1] == 0x8D:
        disp = struct.unpack("<i", text_data[i+3:i+7])[0]
        insn_rva = text_sec.VirtualAddress + i
        target = insn_rva + 7 + disp
        if target == str_rva:
            print(f"Found LEA reference at RVA 0x{insn_rva:X}")
