exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"

with open(exe_path, "rb") as f:
    data = f.read()

start = 0x258DDE0
end = 0x258F000
chunk = data[start:end]

# Extract null-terminated ascii strings
curr = bytearray()
for b in chunk:
    if 32 <= b <= 126:
        curr.append(b)
    else:
        if len(curr) >= 3:
            print(curr.decode('latin-1'))
        curr = bytearray()
