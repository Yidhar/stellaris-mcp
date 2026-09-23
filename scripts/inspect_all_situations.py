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
cnt = r32(mgr + 0x20)
print(f"Global Situation Entity Manager at [base + 0x32877E0]: 0x{mgr:X}, cnt={cnt}")

for i in range(cnt):
    sit_ptr = r64(arr + i * 16 + 8)
    if sit_ptr and sit_ptr > 0x10000:
        sit_id = r32(sit_ptr + 8)
        vt = r64(sit_ptr)
        print(f"\n--- Situation[{i}]: ptr=0x{sit_ptr:X}, id={sit_id}, vt=0x{vt-base:X} ---")
        # Dump fields of CSituation
        for off in range(0, 0x100, 8):
            v = r64(sit_ptr + off)
            s = extract_str(sit_ptr + off)
            s_info = f" str='{s}'" if s else ""
            print(f"  +0x{off:02X}: 0x{v:016X}{s_info}")
