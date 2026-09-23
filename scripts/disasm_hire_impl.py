import pefile, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

off = rva_to_offset(0x848490)
code = data[off : off + 0x200]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
print("Disassembly of CCountryLeaderManager::HireLeader (0x848490):")
for insn in md.disasm(code, 0x140848490):
    print(f"  0x{insn.address:X}:  {insn.mnemonic:8s} {insn.op_str}")

