import struct

with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb") as f:
    data = f.read()

# Search for 0x3B3C (in little-endian: 3C 3B) or 0x3832 (32 38)
# Typically registered as: RegisterCommand(0x3B3C, ...)
# e.g., mov edx, 0x3B3C -> BA 3C 3B 00 00
needle = b"\xBA\x3C\x3B\x00\x00"
pos = 0
while True:
    idx = data.find(needle, pos)
    if idx == -1: break
    print(f"Found mov edx, 0x3B3C at raw offset 0x{idx:X}")
    pos = idx + 1

needle2 = b"\xBA\x32\x38\x00\x00"
pos = 0
while True:
    idx = data.find(needle2, pos)
    if idx == -1: break
    print(f"Found mov edx, 0x3832 at raw offset 0x{idx:X}")
    pos = idx + 1
