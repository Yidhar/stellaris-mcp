import capstone, struct

with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb") as f:
    data = f.read()

import pefile
pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

def read_qword(rva):
    off = rva_to_offset(rva)
    return struct.unpack("<Q", data[off:off+8])[0]

vt_rva = 0x2566520
print(f"=== CAssignLeaderCommand Vtable at RVA 0x{vt_rva:X} ===")
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for i in range(10):
    fn_va = read_qword(vt_rva + i * 8)
    fn_rva = fn_va - 0x140000000
    print(f"vfunc[{i}]: VA 0x{fn_va:X} (RVA 0x{fn_rva:X})")
    
# Usually vfunc[1] or vfunc[3] is Execute / IsValid
# Let's disassemble the first 100 instructions of vfunc[1] and vfunc[3]
for idx in [1, 2, 3]:
    fn_va = read_qword(vt_rva + idx * 8)
    fn_rva = fn_va - 0x140000000
    off = rva_to_offset(fn_rva)
    code = data[off : off + 200]
    print(f"\n--- Disassembly of vfunc[{idx}] (RVA 0x{fn_rva:X}) ---")
    for insn in md.disasm(code, fn_va):
        print(f"  0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")
        if insn.mnemonic in ['ret']:
            break
