import pefile
import struct
import capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)
data = open(exe_path, "rb").read()

def rva_to_offset(rva):
    for s in pe.sections:
        va = s.VirtualAddress
        sz = s.Misc_VirtualSize
        if va <= rva < va + sz:
            return rva - va + s.PointerToRawData
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

off = rva_to_offset(0x2418C58)
funcs = struct.unpack_from("<10Q", data, off)
print("=== Vtable at 0x2418C58 ===")
for i, f in enumerate(funcs):
    f_rva = f - pe.OPTIONAL_HEADER.ImageBase
    print(f"  [{i}] RVA 0x{f_rva:X}")

# Let's see [0] (Execute?)
rva_exec = funcs[0] - pe.OPTIONAL_HEADER.ImageBase
off_exec = rva_to_offset(rva_exec)
code = data[off_exec:off_exec+250]
print(f"\n=== Disassembly of [0] (0x{rva_exec:X}) ===")
for insn in md.disasm(code, rva_exec):
    print(f"  0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
    if insn.mnemonic == 'ret': break
