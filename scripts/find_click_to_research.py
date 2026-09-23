import pefile
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

target_str = b"CLICK_TO_RESEARCH\x00"
str_off = data.find(target_str)
str_rva = offset_to_rva(str_off)
print(f"'CLICK_TO_RESEARCH' string RVA: 0x{str_rva:X}")

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_end = text_start + text_sec.SizeOfRawData
text_data = data[text_start:text_end]

for i in range(len(text_data) - 7):
    if text_data[i] == 0x48 and text_data[i+1] == 0x8D:
        disp = struct.unpack("<i", text_data[i+3:i+7])[0]
        insn_rva = text_sec.VirtualAddress + i
        target = insn_rva + 7 + disp
        if target == str_rva:
            print(f"Found LEA reference at RVA 0x{insn_rva:X}")
