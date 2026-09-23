import ctypes, struct, sys
sys.stdout.reconfigure(encoding='utf-8')

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory
pid = 104400
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read)):
        return struct.unpack('<I', buf.raw)[0]
    return 0

def read_str_safe(addr):
    buf = ctypes.create_string_buffer(128)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 128, ctypes.byref(read)):
        raw = buf.raw[:read.value]
        # Check if PdxString: +0x10 size, +0x18 cap
        if len(raw) >= 32:
            sz = struct.unpack('<Q', raw[16:24])[0]
            cap = struct.unpack('<Q', raw[24:32])[0]
            if 0 < sz < 128:
                if cap < 16:
                    return raw[:sz].decode('utf-8', errors='replace')
                else:
                    ptr = struct.unpack('<Q', raw[:8])[0]
                    if ptr and 0x10000 < ptr < 0x7FFFFFFFFFFF:
                        sbuf = ctypes.create_string_buffer(sz)
                        if ReadProcessMemory(hProc, ctypes.c_void_p(ptr), sbuf, sz, ctypes.byref(read)):
                            return sbuf.raw[:read.value].decode('utf-8', errors='replace')
        # Check raw null-terminated string
        s = raw.split(b'\x00')[0]
        if len(s) > 2 and all(32 <= b <= 126 for b in s):
            return s.decode('ascii')
    return ""

ruler_ptr = 0x179df9eb378

print("Scanning ruler pointers for trait_*...")
for off in range(0, 0x800, 8):
    q = r64(ruler_ptr + off)
    if 0x17000000000 <= q <= 0x18000000000:
        # Check direct string
        s = read_str_safe(q)
        if "trait_" in s:
            print(f"Ruler + 0x{off:X} -> 0x{q:X}: '{s}'")
        # Check if object at q has string at +0x18, +0x20, +0x28, +0x30, etc.
        for child_off in range(0, 0x80, 8):
            cq = r64(q + child_off)
            cs = read_str_safe(q + child_off)
            if "trait_" in cs:
                print(f"Ruler + 0x{off:X} -> 0x{q:X} + 0x{child_off:X}: '{cs}'")
            if 0x17000000000 <= cq <= 0x18000000000:
                ccs = read_str_safe(cq)
                if "trait_" in ccs:
                    print(f"Ruler + 0x{off:X} -> 0x{q:X} + 0x{child_off:X} -> 0x{cq:X}: '{ccs}'")

