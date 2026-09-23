import pefile
import struct
import capstone

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)

with open(exe_path, "rb") as f:
    data = f.read()

text_sec = pe.sections[0]
text_data = data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]
text_base = text_sec.VirtualAddress

# Search for 4073 as 16-bit or 32-bit immediate:
# 73 40 00 00 or 73 40
imm32 = struct.pack('<I', 0x4073)
imm16 = struct.pack('<H', 0x4073)

print("Searching for 0x4073 (hire_leader)...")
pos = 0
found = []
while True:
    idx = text_data.find(imm32, pos)
    if idx == -1:
        break
    found.append(idx)
    pos = idx + 1

print(f"Found {len(found)} occurrences of imm32 0x4073 in .text:")
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for off in found:
    rva = text_base + off
    va = 0x140000000 + rva
    # Disassemble 32 bytes before and after
    start_dis = max(0, off - 16)
    code = text_data[start_dis : off + 16]
    print(f"\n--- Occurrence at file offset 0x{text_sec.PointerToRawData + off:X}, RVA 0x{rva:X}, VA 0x{va:X} ---")
    for insn in md.disasm(code, 0x140000000 + text_base + start_dis):
        arrow = " <===" if insn.address <= va < insn.address + insn.size else ""
        print(f"  0x{insn.address:X}:  {insn.mnemonic:8s} {insn.op_str}{arrow}")

