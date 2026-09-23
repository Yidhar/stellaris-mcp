import pefile, struct, capstone

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

target_rva = 0x648970
text_sec = pe.sections[0]
code = data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Look for call rel32 to target_rva
callers = []
for i in range(len(code) - 5):
    if code[i] == 0xE8: # call rel32
        disp = struct.unpack('<i', code[i+1:i+5])[0]
        curr_rva = text_sec.VirtualAddress + i + 5
        if curr_rva + disp == target_rva:
            callers.append(curr_rva - 5)

print(f"Found {len(callers)} direct callers of PostCommand (0x648970):")
# For each caller, look backward for command vtable or cmd ID
for c_rva in callers:
    off = rva_to_offset(c_rva)
    block = code[max(0, off - text_sec.PointerToRawData - 80) : off - text_sec.PointerToRawData + 10]
    # Check if any instructions mention 0x253... or 0x407...
    insns = list(md.disasm(block, 0x140000000 + max(0, c_rva - 80)))
    relevant = False
    for ins in insns:
        if any(k in ins.op_str for k in ['2530f', '2530', '4073', '4074', '4076', '4292', 'leader']):
            relevant = True
            break
    if relevant or len(callers) < 30:
        print(f"\nCaller at RVA 0x{c_rva:X} (VA 0x{0x140000000 + c_rva:X}):")
        for ins in insns[-8:]:
            print(f"  0x{ins.address:X}:  {ins.mnemonic:8s} {ins.op_str}")

