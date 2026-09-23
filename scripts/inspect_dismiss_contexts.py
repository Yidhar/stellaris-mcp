with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb") as f:
    data = f.read()

offsets = [0x24a6788, 0x24c268c, 0x24c4460, 0x253fd4a, 0x258f380]

for off in offsets:
    raw = data[off : off + 64].split(b'\x00')[0]
    print(f"Offset 0x{off:X}: '{raw.decode('latin-1', errors='ignore')}'")
    # Context around this offset
    ctx = data[max(0, off - 32) : off + 64]
    # print string table around it
    parts = ctx.split(b'\x00')
    clean = [p.decode('latin-1', errors='ignore') for p in parts if p]
    print(f"    Context: {clean}")

