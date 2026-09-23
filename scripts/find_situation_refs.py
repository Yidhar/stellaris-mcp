import pefile
import struct

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

def find_string_refs(target_str):
    pos = data.find(target_str.encode('latin-1') + b'\x00')
    if pos == -1:
        print(f"String '{target_str}' not found")
        return []
    print(f"String '{target_str}' at file offset 0x{pos:X}")
    
    # RVA of string
    str_rva = None
    for sec in pe.sections:
        if sec.PointerToRawData <= pos < sec.PointerToRawData + sec.SizeOfRawData:
            str_rva = sec.VirtualAddress + (pos - sec.PointerToRawData)
            break
    print(f"String RVA: 0x{str_rva:X}")
    
    text_sec = pe.sections[0]
    text_data = data[text_sec.PointerToRawData:text_sec.PointerToRawData+text_sec.SizeOfRawData]
    
    refs = []
    for i in range(len(text_data) - 7):
        rva = text_sec.VirtualAddress + i
        for op_len in [7, 6, 5]:
            disp_off = i + op_len - 4
            disp = struct.unpack('<i', text_data[disp_off:disp_off+4])[0]
            if rva + op_len + disp == str_rva:
                refs.append(rva)
    return refs

for s in ["CAN_SET_SITUATION_APPROACH", "CANNOT_SET_SITUATION_APPROACH", "CHANGE_SITUATION_TARGET_EFFECT"]:
    refs = find_string_refs(s)
    for r in refs:
        print(f"  Reference at RVA 0x{r:X}")
