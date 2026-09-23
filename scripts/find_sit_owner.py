import ctypes, struct

kernel32 = ctypes.WinDLL('kernel32')
h = kernel32.OpenProcess(0x10, False, 73956)
base = 0x7FF75ED50000

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read))
    return struct.unpack('<Q', buf.raw)[0]

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read))
    return struct.unpack('<I', buf.raw)[0]

mgr = r64(base + 0x32877E0)
arr = r64(mgr + 0x18)

c_mgr = r64(base + 0x3287788)
c_arr = r64(c_mgr + 0x18)
c_cnt = r32(c_mgr + 0x20)
countries = {}
for ci in range(c_cnt):
    cp = r64(c_arr + ci * 16 + 8)
    if cp:
        cid = r32(cp + 0x20)
        countries[cp] = cid

print(f"Total countries: {len(countries)}")

for i in range(5):
    sit = r64(arr + i * 16 + 8)
    if not sit: continue
    print(f"\nScanning Situation[{i}] at 0x{sit:X}:")
    for off in range(0, 0x300, 8):
        val = r64(sit + off)
        if val in countries:
            print(f"  Country pointer found at +0x{off:03X}: 0x{val:X} (Country ID {countries[val]})")
        # Check if 32-bit ID matches country ID
        v32 = r32(sit + off)
        if v32 in countries.values() and off not in [0, 8]:
            pass
