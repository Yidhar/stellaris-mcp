import pefile, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

def offset_to_rva(off):
    for sec in pe.sections:
        if sec.PointerToRawData <= off < sec.PointerToRawData + sec.SizeOfRawData:
            return sec.VirtualAddress + (off - sec.PointerToRawData)
    return None

def get_str(rva):
    off = rva_to_offset(rva)
    if not off: return ""
    s = data[off : off + 64]
    idx = s.find(b'\0')
    if idx != -1: s = s[:idx]
    return s.decode('ascii', errors='replace')

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Disassemble all command registrations
code = data[0x18C000 : 0x197000]
base_rva = offset_to_rva(0x18C000)

commands = []
curr_opcode = None
curr_str_rva = None

for insn in md.disasm(code, 0x140000000 + base_rva):
    if insn.mnemonic == "mov" and insn.op_str.startswith("edx, 0x"):
        try:
            curr_opcode = int(insn.op_str.split("0x")[1], 16)
        except: pass
    elif insn.mnemonic == "lea" and "r8," in insn.op_str and "rip" in insn.op_str:
        target = insn.address + insn.size + int(insn.op_str.split("+")[1].rstrip("]"), 16)
        curr_str_rva = target - 0x140000000
    elif insn.mnemonic == "call" and curr_opcode is not None:
        name = get_str(curr_str_rva) if curr_str_rva else "unknown"
        commands.append((curr_opcode, name))
        curr_opcode = None

print(f"Total registered commands found: {len(commands)}")
print("\nCommands related to design, ship, fleet, upgrade:")
for op, name in commands:
    if any(k in name.lower() for k in ("design", "ship", "fleet", "upgrade")):
        print(f"  Opcode: 0x{op:04X} -> '{name}'")
