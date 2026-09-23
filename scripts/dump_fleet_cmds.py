import pefile, struct, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def offset_to_rva(off):
    for sec in pe.sections:
        if sec.PointerToRawData <= off < sec.PointerToRawData + sec.SizeOfRawData:
            return sec.VirtualAddress + (off - sec.PointerToRawData)
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Look at 0x192000 to 0x193000 (where 0x3B3C is at 0x1925F4)
code = data[0x192000 : 0x193000]
base_rva = offset_to_rva(0x192000)

commands = []
curr_opcode = None
curr_func = None

for insn in md.disasm(code, 0x140000000 + base_rva):
    if insn.mnemonic == "mov" and insn.op_str.startswith("edx, 0x"):
        try:
            curr_opcode = int(insn.op_str.split("0x")[1], 16)
        except: pass
    elif insn.mnemonic == "lea" and "r8," in insn.op_str:
        curr_func = insn.address + insn.size + int(insn.op_str.split("+")[1].rstrip("]"), 16) if "+" in insn.op_str else 0
    elif insn.mnemonic == "call" and curr_opcode is not None:
        commands.append((curr_opcode, hex(insn.address), hex(curr_func) if curr_func else "none"))
        curr_opcode = None

print(f"Commands near 0x3B3C (total {len(commands)}):")
for op, addr, func in commands:
    print(f"  Opcode: 0x{op:04X} at {addr}, factory: {func}")
