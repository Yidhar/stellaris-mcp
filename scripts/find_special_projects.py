import pefile

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)
with open(exe_path, "rb") as f:
    data = f.read()

target = b"special_project"
idx = 0
found = 0
while found < 20:
    idx = data.find(target, idx)
    if idx == -1: break
    rva = pe.get_rva_from_offset(idx)
    s = data[idx:idx+40].split(b"\0")[0].decode("utf-8", errors="ignore")
    print(f"Found '{s}' at RVA 0x{rva:X}")
    idx += len(target)
    found += 1
