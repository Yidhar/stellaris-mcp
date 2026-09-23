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

mgr = r64(base + 0x32877E0)
arr = r64(mgr + 0x18)
sit_ptr = r64(arr + 8) # Situation 0
print(f"Situation 0: 0x{sit_ptr:X}")

for off in range(0x100, 0x300, 8):
    v = r64(sit_ptr + off)
    s = extract_str(sit_ptr + off)
    s_info = f" str='{s}'" if s else ""
    # check if float
    f_val = struct.unpack('<f', struct.pack('<I', v & 0xFFFFFFFF))[0]
    f_info = f" float={f_val:.2f}" if -1000.0 < f_val < 1000.0 and abs(f_val) > 0.001 else ""
    # check if pointer to vtable or string
    extra = ""
    if v and 0x10000 < v < 0x7FFFFFFFFFFF:
        v_s = extract_str(v) or extract_str(v + 8) or extract_str(v + 0x18) or extract_str(v + 0x20)
        if v_s: extra = f" -> ptr_str='{v_s}'"
    print(f"  +0x{off:03X}: 0x{v:016X}{s_info}{f_info}{extra}")
