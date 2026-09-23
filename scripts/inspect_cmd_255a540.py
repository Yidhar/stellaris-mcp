import pefile, struct, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

off = rva_to_offset(0x255A540)
print(f"Vtable at RVA 0x255A540 (offset 0x{off:X}):")
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for i in range(12):
    fn_va = struct.unpack('<Q', data[off + i*8 : off + (i+1)*8])[0]
    fn_rva = fn_va - 0x140000000
    print(f"  vfunc[{i:2d}]: 0x{fn_va:X} (RVA 0x{fn_rva:X})")

exec_rva = struct.unpack('<Q', data[off + 9*8 : off + 10*8])[0] - 0x140000000
print(f"\nDisassembly of Execute at RVA 0x{exec_rva:X}:")
exec_off = rva_to_offset(exec_rva)
for insn in md.disasm(data[exec_off : exec_off + 0x80], 0x140000000 + exec_rva):
    print(f"  0x{insn.address:X}:  {insn.mnemonic:8s} {insn.op_str}")

# Also check GetType at vfunc[10]
gettype_rva = struct.unpack('<Q', data[off + 10*8 : off + 11*8])[0] - 0x140000000
print(f"\nDisassembly of GetType at RVA 0x{gettype_rva:X}:")
gt_off = rva_to_offset(gettype_rva)
for insn in md.disasm(data[gt_off : gt_off + 0x20], 0x140000000 + gettype_rva):
    print(f"  0x{insn.address:X}:  {insn.mnemonic:8s} {insn.op_str}")

