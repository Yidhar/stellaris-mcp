import capstone

with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb") as f:
    data = f.read()

import pefile
pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Disassemble 0x140537850 for 0x3000 bytes
start_va = 0x140537850
off = rva_to_offset(start_va - 0x140000000)
code = data[off : off + 0x6000]

instructions = list(md.disasm(code, start_va))
print(f"Disassembled {len(instructions)} instructions.")

# Find all comparisons with r8d or immediate values around 0x4076
for i, insn in enumerate(instructions):
    if insn.mnemonic in ['cmp', 'sub', 'xor'] and ('4076' in insn.op_str or '16502' in insn.op_str):
        print(f"\nMatch at index {i}, VA 0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
        for j in range(max(0, i-5), min(len(instructions), i+30)):
            print(f"  0x{instructions[j].address:X}: {instructions[j].mnemonic:8s} {instructions[j].op_str}")
