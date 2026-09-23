with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb") as f:
    data = f.read()

start = 0x24c43e0
end = 0x24c4550
raw = data[start:end]
print("Strings around 0x24c4400:")
# print null-separated strings with offset
pos = 0
while pos < len(raw):
    idx = raw.find(b'\x00', pos)
    if idx == -1:
        break
    s = raw[pos:idx]
    if s:
        print(f"  Offset 0x{start + pos:X}: {s.decode('latin-1', errors='ignore')}")
    pos = idx + 1

