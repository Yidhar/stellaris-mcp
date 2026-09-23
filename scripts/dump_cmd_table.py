import pefile, capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_rva = text_sec.VirtualAddress

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

start_rva = 0x1A2000
length = 0x3000
start_off = text_start + (start_rva - text_rva)
code = data[start_off:start_off + length]

cur_str = None
for insn in cs.disasm(code, start_rva):
    if insn.mnemonic == 'lea' and insn.op_str.startswith('r8, [rip + '):
        disp = int(insn.op_str.split(' + ')[1].rstrip(']'), 16)
        str_rva = insn.address + insn.size + disp
        for s in pe.sections:
            if s.VirtualAddress <= str_rva < s.VirtualAddress + s.SizeOfRawData:
                off = s.PointerToRawData + (str_rva - s.VirtualAddress)
                cur_str = data[off:off+100].split(b'\x00')[0].decode('latin-1', errors='ignore')
                break
    elif insn.mnemonic == 'mov' and insn.op_str.startswith('edx, '):
        try:
            cmd_id = int(insn.op_str.split(', ')[1], 16)
            if cur_str:
                print(f"ID 0x{cmd_id:04X} ({cmd_id}): {cur_str}")
                cur_str = None
        except:
            pass
