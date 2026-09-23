import pefile, capstone, struct

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def offset_to_rva(offset):
    for sec in pe.sections:
        if sec.PointerToRawData <= offset < sec.PointerToRawData + sec.SizeOfRawData:
            return sec.VirtualAddress + (offset - sec.PointerToRawData)
    return None

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

# String 'dismiss_leader_button' is at 0x258F380 (offset)
str_off = 0x258F380
str_rva = offset_to_rva(str_off)
str_va = 0x140000000 + str_rva

print(f"String 'dismiss_leader_button' at offset 0x{str_off:X}, RVA 0x{str_rva:X}, VA 0x{str_va:X}")

# Search direct 64-bit pointers
p = struct.pack('<Q', str_va)
pos = 0
while True:
    idx = data.find(p, pos)
    if idx == -1: break
    print(f"  Direct ptr at offset 0x{idx:X} (RVA 0x{offset_to_rva(idx):X})")
    pos = idx + 1

# Search RIP-relative in .text
text_sec = pe.sections[0]
code_raw = data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]
for i in range(len(code_raw) - 7):
    disp = struct.unpack('<i', code_raw[i+3:i+7])[0]
    curr_rva = text_sec.VirtualAddress + i + 7
    if curr_rva + disp == str_rva:
        insn_va = 0x140000000 + curr_rva - 7
        print(f"  RIP-relative ref at VA 0x{insn_va:X} (RVA 0x{curr_rva - 7:X})")

