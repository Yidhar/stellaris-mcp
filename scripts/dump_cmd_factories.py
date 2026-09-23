import pefile, struct, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Disassemble from 0x140193100 to 0x140193350
off_start = rva_to_offset(0x193100)
code = data[off_start : off_start + 0x300]

commands = []
curr_opcode = None
curr_factory = None

for insn in md.disasm(code, 0x140193100):
    if insn.mnemonic == "mov" and insn.op_str.startswith("edx, 0x"):
        curr_opcode = int(insn.op_str.split("0x")[1], 16)
    elif insn.mnemonic == "lea" and "r8," in insn.op_str and "rip" in insn.op_str:
        # compute rip target
        target = insn.address + insn.size + int(insn.op_str.split("+")[1].rstrip("]"), 16)
        curr_factory = target
    elif insn.mnemonic == "call" and curr_opcode is not None:
        commands.append((curr_opcode, curr_factory))
        curr_opcode = None

print(f"Disassembling factory functions for {len(commands)} commands:")
for op, f_addr in commands:
    # Read factory function code
    f_rva = f_addr - 0x140000000
    f_off = rva_to_offset(f_rva)
    if f_off:
        f_code = data[f_off : f_off + 30]
        # Look for vtable lea or mov
        vtable_target = None
        for f_insn in md.disasm(f_code, f_addr):
            if f_insn.mnemonic in ("lea", "mov") and "rip" in f_insn.op_str:
                vtable_target = f_insn.address + f_insn.size + int(f_insn.op_str.split("+")[1].rstrip("]"), 16)
                break
        print(f"  Opcode 0x{op:04X}: factory={hex(f_addr)}, vtable={hex(vtable_target) if vtable_target else 'none'}")
