import pefile, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def offset_to_rva(off):
    for sec in pe.sections:
        if sec.PointerToRawData <= off < sec.PointerToRawData + sec.SizeOfRawData:
            return sec.VirtualAddress + (off - sec.PointerToRawData)
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

off = 0x1925F4
rva = offset_to_rva(off)
code = data[off - 30 : off + 80]

print(f"Disassembly around 0x1925F4 (RVA 0x{rva:X}):")
for insn in md.disasm(code, 0x140000000 + rva - 30):
    print(f"0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")
