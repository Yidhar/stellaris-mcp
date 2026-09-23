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

def extract_str(addr):
    buf = ctypes.create_string_buffer(32)
    read = ctypes.c_size_t()
    if not kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 32, ctypes.byref(read)):
        return ""
    raw = buf.raw
    sz = struct.unpack('<Q', raw[16:24])[0]
    cap = struct.unpack('<Q', raw[24:32])[0]
    if sz == 0 or sz > 500: return ""
    if cap < 16:
        return raw[:min(sz, 15)].decode('utf-8', errors='ignore')
    else:
        ptr = struct.unpack('<Q', raw[:8])[0]
        if ptr and 0x10000 < ptr < 0x7FFFFFFFFFFF:
            sbuf = ctypes.create_string_buffer(min(sz, 256))
            if kernel32.ReadProcessMemory(h, ctypes.c_void_p(ptr), sbuf, min(sz, 256), ctypes.byref(read)):
                return sbuf.raw[:read.value].decode('utf-8', errors='ignore')
    return ""

def get_str_at(ptr):
    if not ptr or ptr < 0x10000 or ptr > 0x7FFFFFFFFFFF: return ""
    for off in [0x00, 0x08, 0x10, 0x18, 0x20, 0x28]:
        s = extract_str(ptr + off)
        if s and len(s) >= 2: return s
    return ""

mgr = r64(base + 0x32877E0)
arr = r64(mgr + 0x18)
cnt = r32(mgr + 0x20)

print(f"Total situations in manager: {cnt}")
for i in range(cnt):
    sit = r64(arr + i * 16 + 8)
    if not sit: continue
    sit_id = r32(sit + 8)
    type_ptr = r64(sit + 0x208)
    app_ptr = r64(sit + 0x210)
    type_key = get_str_at(type_ptr)
    app_key = get_str_at(app_ptr)
    
    # Check target / owner country
    # Let's check offsets 0x1A0..0x208
    owner_id = r32(sit + 0x1B8) # or 0x1C0
    target_id = r32(sit + 0x100) # or check offsets
    # Let's print candidate IDs and floats
    print(f"\n[Situation ID {sit_id}] ptr=0x{sit:X}")
    print(f"  Type: {type_key} (0x{type_ptr:X})")
    print(f"  Approach: {app_key} (0x{app_ptr:X})")
    
    # Find progress float
    for off in range(0x180, 0x250, 4):
        val = r32(sit + off)
        # float?
        f_val = struct.unpack('<f', struct.pack('<I', val))[0]
        if 0.0 < f_val <= 1000.0:
            print(f"  Float at +0x{off:03X}: {f_val:.2f}")
        elif 0 < val < 50:
            print(f"  Int at +0x{off:03X}: {val}")
