import pefile, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

f_rva = 0x142301030 - 0x140000000
f_off = rva_to_offset(f_rva)
code = data[f_off : f_off + 40]

print("Disassembly of 0x142301030:")
for insn in md.disasm(code, 0x142301030):
    print(f"0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")
