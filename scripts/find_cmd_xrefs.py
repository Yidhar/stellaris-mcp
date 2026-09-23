import pefile
import struct

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

image_base = 0x140000000

# Convert file offset in .rdata to RVA
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

targets = {
    "hire_leader": 0x24C4420,
    "dismiss_leader": 0x24C4460,
    "assign_leader_command": 0x24C4478
}

for name, off in targets.items():
    rva = offset_to_rva(off)
    va = image_base + rva
    print(f"\nTarget '{name}': file offset 0x{off:X}, RVA 0x{rva:X}, VA 0x{va:X}")
    
    # 1. Search for direct 64-bit pointer
    packed_va = struct.pack('<Q', va)
    pos = 0
    while True:
        idx = data.find(packed_va, pos)
        if idx == -1:
            break
        idx_rva = offset_to_rva(idx)
        print(f"  Direct 64-bit pointer at file offset 0x{idx:X}, RVA 0x{idx_rva:X}")
        pos = idx + 1
        
    # 2. Search for RIP-relative references (lea rdx, [rip + disp32])
    # in 64-bit code: disp32 = target_rva - (instruction_rva + 7)
    # let's search across all code sections
    code_sec = pe.sections[0] # .text
    code_raw = data[code_sec.PointerToRawData : code_sec.PointerToRawData + code_sec.SizeOfRawData]
    for i in range(len(code_raw) - 7):
        # check if 4 bytes at i+3 form rip disp
        disp = struct.unpack('<i', code_raw[i+3:i+7])[0]
        curr_rva = code_sec.VirtualAddress + i + 7
        if curr_rva + disp == rva:
            instr_bytes = code_raw[i:i+7].hex()
            print(f"  RIP-relative ref at RVA 0x{curr_rva - 7:X} (offset 0x{code_sec.PointerToRawData + i:X}): {instr_bytes}")

