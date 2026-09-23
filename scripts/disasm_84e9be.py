import pefile, capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f: data = f.read()

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_rva = text_sec.VirtualAddress

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

target_rva = 0x84E9BE
start_rva = target_rva - 0x50
start_off = text_start + (start_rva - text_rva)
code = data[start_off:start_off + 0x120]

print(f"=== Disassembly around 0x84E9BE ===")
for insn in cs.disasm(code, start_rva):
    prefix = ">>> " if insn.address == target_rva else "    "
    print(f"{prefix}0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")
