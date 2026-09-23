import ctypes, struct, sys
sys.stdout.reconfigure(encoding='utf-8')

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory
pid = 104400
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

for addr, name in [(0x178BED02170, "+0x1DF0 (cnt=4)"), (0x178F9EA8AE0, "+0x1C60 (cnt=13)"), (0x178F9EA9460, "+0x1E20 (cnt=13)")]:
    buf = ctypes.create_string_buffer(128)
    read = ctypes.c_size_t()
    ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 128, ctypes.byref(read))
    raw = buf.raw[:read.value]
    print(f"\n=== {name} at 0x{addr:X} ===")
    for i in range(0, min(len(raw), 64), 4):
        u32 = struct.unpack('<I', raw[i:i+4])[0]
        print(f"  [{i//4:2d}] off +{hex(i)}: {u32} (0x{u32:X})")

