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

for i in range(5):
    sit = r64(arr + i * 16 + 8)
    owner = r32(sit + 0x204)
    type_name = get_str_at(r64(sit + 0x208))
    app_name = get_str_at(r64(sit + 0x210))
    print(f"\n--- Situation {i}: id={r32(sit+8)}, owner={owner}, type='{type_name}', app='{app_name}' ---")
    for off in range(0x200, 0x2B0, 8):
        v = r64(sit + off)
        # check float
        f1 = struct.unpack('<f', struct.pack('<I', v & 0xFFFFFFFF))[0]
        f2 = struct.unpack('<f', struct.pack('<I', (v >> 32) & 0xFFFFFFFF))[0]
        f_info = []
        if -1000.0 < f1 < 1000.0 and abs(f1) > 0.001: f_info.append(f"f1={f1:.2f}")
        if -1000.0 < f2 < 1000.0 and abs(f2) > 0.001: f_info.append(f"f2={f2:.2f}")
        f_str = f" ({', '.join(f_info)})" if f_info else ""
        print(f"  +0x{off:03X}: 0x{v:016X}{f_str}")
