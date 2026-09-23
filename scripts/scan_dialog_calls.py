import pefile, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

off = rva_to_offset(0x1069180)
code = data[off : off + 0x600]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

calls = []
leas = []
for insn in md.disasm(code, 0x141069180):
    if insn.mnemonic == 'call':
        calls.append((insn.address, insn.op_str))
    elif insn.mnemonic == 'lea' and 'rip +' in insn.op_str:
        # Check target
        disp = int(insn.op_str.split('rip + ')[1].rstrip(']'), 16)
        target = insn.address + insn.size + disp
        leas.append((insn.address, insn.op_str, target))

print(f"Calls in 0x1069180: {len(calls)}")
for a, op in calls:
    print(f"  0x{a:X}: call {op}")

print(f"\nLEAs in 0x1069180: {len(leas)}")
for a, op, target in leas:
    print(f"  0x{a:X}: {op} -> 0x{target:X}")

