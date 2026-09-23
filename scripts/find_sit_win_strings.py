import pefile, capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_rva = text_sec.VirtualAddress

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

start_rva = 0x1D7A000
end_rva = 0x1D8A000
code = data[text_start + (start_rva - text_rva) : text_start + (end_rva - text_rva)]

strings_found = set()
for insn in cs.disasm(code, start_rva):
    if insn.mnemonic == 'lea' and '[rip + ' in insn.op_str:
        # get disp
        try:
            disp_str = insn.op_str.split('[rip + ')[1].rstrip(']')
            disp = int(disp_str, 16)
            target = insn.address + insn.size + disp
            # check if target points to a valid ascii string
            for s in pe.sections:
                if s.VirtualAddress <= target < s.VirtualAddress + s.SizeOfRawData:
                    off = s.PointerToRawData + (target - s.VirtualAddress)
                    cand = data[off:off+60].split(b'\x00')[0]
                    if len(cand) >= 3 and all(32 <= b <= 126 for b in cand):
                        s_str = cand.decode('ascii')
                        if any(k in s_str.lower() for k in ['sit', 'proj', 'anom', 'entry', 'list', 'tab', 'item', 'log']):
                            if s_str not in strings_found:
                                strings_found.add(s_str)
                                print(f"0x{insn.address:X}: \"{s_str}\"")
        except:
            pass
