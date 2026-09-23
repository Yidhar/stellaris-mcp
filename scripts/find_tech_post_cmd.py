import pefile
import struct

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_end = text_start + text_sec.SizeOfRawData
text_data = data[text_start:text_end]

post_cmd_rva = 0x648970
print(f"Searching for calls to PostCommand (0x{post_cmd_rva:X}) in range 0x1050000 - 0x1070000...")

for i in range(len(text_data) - 5):
    insn_rva = text_sec.VirtualAddress + i
    if 0x1050000 <= insn_rva <= 0x1070000:
        if text_data[i] == 0xE8:
            disp = struct.unpack("<i", text_data[i+1:i+5])[0]
            target = insn_rva + 5 + disp
            if target == post_cmd_rva:
                print(f"  Found PostCommand call at RVA 0x{insn_rva:X}")
