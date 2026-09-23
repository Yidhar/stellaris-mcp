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

mgr = r64(base + 0x3287788)
arr = r64(mgr + 0x18)
country = r64(arr + 8)
print(f"Player Country: 0x{country:X}")

print("=== Scanning Country Vectors & Pointers from 0x1500 to 0x2500 ===")
for off in range(0x1500, 0x2500, 8):
    val = r64(country + off)
    # Check if vector: [val, val+8, val+16]
    if off % 24 == 0 or True:
        p0 = r64(country + off)
        p1 = r64(country + off + 8)
        p2 = r64(country + off + 16)
        # Check if std::vector of pointers or structs
        if p0 and p1 and p2 and 0x10000 < p0 <= p1 <= p2 and (p1 - p0) <= 1000000:
            if (p2 - p0) % 8 == 0 and (p1 - p0) % 8 == 0:
                cnt = (p1 - p0) // 8
                if 0 < cnt < 500:
                    print(f"  Vector at +0x{off:04X}: count={cnt} [0x{p0:X} .. 0x{p1:X}]")
    if base < val < base + 0x3000000:
        print(f"  Vtable/Func at +0x{off:04X}: 0x{val:X} (RVA 0x{val - base:X})")
