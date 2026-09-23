import pefile, capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)
with open(exe_path, "rb") as f: data = f.read()

text_sec = pe.sections[0]
text_rva = text_sec.VirtualAddress
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

start_rva = 0x14D6000
end_rva = 0x14E4000
code = data[text_sec.PointerToRawData + (start_rva - text_rva) : text_sec.PointerToRawData + (end_rva - text_rva)]

for insn in cs.disasm(code, start_rva):
    if insn.mnemonic == 'lea' and '[rip +' in insn.op_str:
        try:
            disp = int(insn.op_str.split('[rip + ')[1].rstrip(']'), 16)
            target = insn.address + insn.size + disp
            for s in pe.sections:
                if s.VirtualAddress <= target < s.VirtualAddress + s.SizeOfRawData:
                    off = s.PointerToRawData + (target - s.VirtualAddress)
                    cand = data[off:off+40].split(b'\x00')[0]
                    if len(cand) >= 4 and all(32 <= b <= 126 for b in cand):
                        s_name = cand.decode('ascii')
                        if any(k in s_name for k in ['subview', 'situation', 'project', 'anomaly', 'chain', 'stage', 'approach', 'site', 'rift', 'contact']):
                            print(f"0x{insn.address:X}: \"{s_name}\"")
        except:
            pass
