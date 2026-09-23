import pefile, capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)
with open(exe_path, "rb") as f:
    data = f.read()

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_rva = text_sec.VirtualAddress
text_size = text_sec.SizeOfRawData
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

target_rvas = [0x249B8E7, 0x24AF1D9, 0x24B5D06, 0x24CE272]

for off in range(0, text_size - 7):
    # check lea reg, [rip + disp]
    if data[text_start + off] == 0x48 and data[text_start + off + 1] in (0x8D, ) and (data[text_start + off + 2] & 0xC7) == 0x05:
        disp = int.from_bytes(data[text_start + off + 3:text_start + off + 7], "little", signed=True)
        insn_rva = text_rva + off
        next_rva = insn_rva + 7
        target = next_rva + disp
        if target in target_rvas:
            print(f"Ref to 0x{target:X} at RVA 0x{insn_rva:X}")
            code = data[text_start + off - 0x10 : text_start + off + 0x30]
            for insn in cs.disasm(code, insn_rva - 0x10):
                print(f"  0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
