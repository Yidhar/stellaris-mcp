import pefile, capstone, struct

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

text_sec = pe.sections[0]
code = data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]

def offset_to_rva(offset):
    for sec in pe.sections:
        if sec.PointerToRawData <= offset < sec.PointerToRawData + sec.SizeOfRawData:
            return sec.VirtualAddress + (offset - sec.PointerToRawData)
    return None

# Find all occurrences of B8 xx xx 00 00 C3 in .text
pattern = bytearray(b'\xB8\x00\x00\x00\x00\xC3')
results = []
for i in range(len(code) - 6):
    if code[i] == 0xB8 and code[i+5] == 0xC3 and code[i+3] == 0x00 and code[i+4] == 0x00:
        val = struct.unpack('<I', code[i+1:i+5])[0]
        if 0x1000 <= val <= 0x5000:
            off = text_sec.PointerToRawData + i
            rva = offset_to_rva(off)
            results.append((val, rva))

print(f"Found {len(results)} command GetType implementations:")
for val, rva in sorted(results):
    if 0x4060 <= val <= 0x4090:
        print(f"  Cmd ID: 0x{val:04X} ({val:5d}) at RVA 0x{rva:X}")

