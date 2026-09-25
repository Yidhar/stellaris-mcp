import struct

data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

pattern = b"Failed to create a device ["
pos = data.find(pattern)
print("String pos in file:", hex(pos) if pos != -1 else "not found")

if pos != -1:
    # In PE file, let's find the RVA of this string
    # image base is 0x140000000
    import pefile
    pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
    str_rva = None
    for sec in pe.sections:
        if sec.PointerToRawData <= pos < sec.PointerToRawData + sec.SizeOfRawData:
            str_rva = sec.VirtualAddress + (pos - sec.PointerToRawData)
            print(f"String RVA: 0x{str_rva:X}")
            break
    
    if str_rva:
        # Search .text for reference to str_rva
        text_sec = pe.sections[0]
        text_raw = data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]
        for i in range(len(text_raw) - 7):
            if text_raw[i] == 0x48 and text_raw[i+1] == 0x8d: # lea reg, [rip + disp32]
                disp = struct.unpack('<i', text_raw[i+3:i+7])[0]
                cur_rva = text_sec.VirtualAddress + i
                dest = cur_rva + 7 + disp
                if dest == str_rva:
                    print(f"Found reference at RVA 0x{cur_rva:X}")
