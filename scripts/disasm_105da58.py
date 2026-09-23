import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)
with open(exe_path, "rb") as f: data = f.read()

text_sec = pe.sections[0]
raw = text_sec.PointerToRawData
md = Cs(CS_ARCH_X86, CS_MODE_64)

start_rva = 0x105D9F0
sec_off = start_rva - text_sec.VirtualAddress
code = data[raw + sec_off : raw + sec_off + 300]
print(f"--- Disassembly from 0x{start_rva:X} ---")
for insn in md.disasm(code, start_rva):
    print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
    if insn.address > 0x105DB00: break
