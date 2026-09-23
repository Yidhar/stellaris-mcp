import pefile, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def va_to_offset(va):
    rva = va - 0x140000000
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

def get_str(va):
    off = va_to_offset(va)
    if not off: return ''
    s = data[off : off + 64]
    idx = s.find(b'\0')
    if idx != -1: s = s[:idx]
    return s.decode('latin-1', errors='replace')

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

va = 0x14108DADC
off = va_to_offset(va)
for insn in md.disasm(data[off : off + 0x400], va):
    extra = ''
    if insn.mnemonic == 'lea' and 'rip' in insn.op_str and '+' in insn.op_str:
        try:
            disp = int(insn.op_str.split('+')[1].rstrip(']'), 16)
            target = insn.address + insn.size + disp
            s = get_str(target)
            if s and len(s) > 1 and s.isprintable(): extra = f' -> "{s}"'
        except: pass
    print(f"  0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}{extra}")
