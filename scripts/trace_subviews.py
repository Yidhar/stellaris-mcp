import pefile, capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)
with open(exe_path, "rb") as f:
    data = f.read()

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_rva = text_sec.VirtualAddress
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

start_rva = 0x103C700
end_rva = 0x103C850
code = data[text_start + (start_rva - text_rva) : text_start + (end_rva - text_rva)]

for insn in cs.disasm(code, start_rva):
    if insn.mnemonic == "lea" and "rip +" in insn.op_str:
        disp = int(insn.op_str.split("rip + ")[1].rstrip("]"), 16)
        target = insn.address + insn.size + disp
        # check if target is in .rdata
        for s in pe.sections:
            if s.VirtualAddress <= target < s.VirtualAddress + s.SizeOfRawData:
                off = s.PointerToRawData + (target - s.VirtualAddress)
                cand = data[off:off+40].split(b"\0")[0]
                if len(cand) >= 3 and all(32 <= b <= 126 for b in cand):
                    print(f"0x{insn.address:X}: string \"{cand.decode('ascii')}\"")
    elif "qword ptr [r14 +" in insn.op_str and insn.mnemonic == "mov":
        print(f"0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")
