import pefile, struct, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

off = rva_to_offset(0x2590EE8)
print(f"Functor vtable at offset 0x{off:X}:")
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for i in range(8):
    fn_va = struct.unpack('<Q', data[off + i*8 : off + (i+1)*8])[0]
    fn_rva = fn_va - 0x140000000
    print(f"  vfunc[{i}]: 0x{fn_va:X} (RVA 0x{fn_rva:X})")
    
# Disassemble vfunc[0] or vfunc[1] (operator())
exec_off = rva_to_offset(fn_rva)
if exec_off:
    code = data[exec_off : exec_off + 0x80]
    print(f"\nDisassembly of vfunc[{i}] at RVA 0x{fn_rva:X}:")
    for insn in md.disasm(code, fn_va):
        print(f"  0x{insn.address:X}:  {insn.mnemonic:8s} {insn.op_str}")

