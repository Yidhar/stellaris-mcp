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

# Search for RIP relative references to 0x35B4390 in .text
target_rva = 0x35B4390
text_sec = pe.sections[0]
code_raw = data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for i in range(len(code_raw) - 7):
    disp = struct.unpack('<i', code_raw[i+3:i+7])[0]
    curr_rva = text_sec.VirtualAddress + i + 7
    if curr_rva + disp == target_rva:
        insn_va = 0x140000000 + curr_rva - 7
        print(f"Ref at VA 0x{insn_va:X} (RVA 0x{curr_rva - 7:X})")
        # Disassemble around this instruction
        dis_code = code_raw[max(0, i-16) : i+32]
        for insn in md.disasm(dis_code, 0x140000000 + text_sec.VirtualAddress + max(0, i-16)):
            print(f"  0x{insn.address:X}:  {insn.mnemonic:8s} {insn.op_str}")

