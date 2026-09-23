import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

def rva_to_offset(rva):
    for section in pe.sections:
        if section.VirtualAddress <= rva < section.VirtualAddress + section.Misc_VirtualSize:
            return rva - section.VirtualAddress + section.PointerToRawData
    return None

with open(exe_path, "rb") as f:
    data = f.read()

md = Cs(CS_ARCH_X86, CS_MODE_64)

# Disassemble OnAlertClick at 0xA46EC0
off = rva_to_offset(0xA46EC0)
code = data[off:off+200]
print("--- OnAlertClick (0xA46EC0) ---")
for insn in md.disasm(code, 0xA46EC0):
    print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")

# Find references to vtable 0x2551D38 in .text
vt_rva = 0x2551D38
import struct
vt_bytes = struct.pack("<I", vt_rva) # might be lea with disp32
print(f"Searching for references to vtable 0x{vt_rva:X}...")
