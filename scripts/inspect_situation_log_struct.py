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

def r_pdx_str(addr):
    cap = r64(addr + 0x10)
    size = r64(addr + 0x8)
    if size == 0 or size > 5000:
        return ""
    if cap < 16:
        buf = ctypes.create_string_buffer(16)
        read = ctypes.c_size_t()
        kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, min(size, 15), ctypes.byref(read))
        return buf.raw[:read.value].decode('utf-8', errors='ignore')
    else:
        ptr = r64(addr)
        if ptr and 0x10000 < ptr < 0x7FFFFFFFFFFF:
            buf = ctypes.create_string_buffer(min(size, 256))
            read = ctypes.c_size_t()
            kernel32.ReadProcessMemory(h, ctypes.c_void_p(ptr), buf, min(size, 256), ctypes.byref(read))
            return buf.raw[:read.value].decode('utf-8', errors='ignore')
    return ""

log_ptr = r64(base + 0x32865F8)
print(f"CSituationLog pointer: 0x{log_ptr:X}")

# 1. Event Chains (+0x18, +0x24)
ec_arr = r64(log_ptr + 0x18)
ec_cnt = r32(log_ptr + 0x24)
print(f"Event Chains: count={ec_cnt}, arr=0x{ec_arr:X}")
for i in range(ec_cnt):
    elem = r64(ec_arr + i * 8)
    print(f"  EC[{i}]: 0x{elem:X}")

# 2. Special Projects (+0x30, +0x3C)
sp_arr = r64(log_ptr + 0x30)
sp_cnt = r32(log_ptr + 0x3C)
print(f"Special Projects: count={sp_cnt}, arr=0x{sp_arr:X}")
for i in range(sp_cnt):
    elem = r64(sp_arr + i * 8)
    print(f"  SP[{i}]: 0x{elem:X}")

# 3. Situations (+0x48, +0x54)
sit_arr = r64(log_ptr + 0x48)
sit_cnt = r32(log_ptr + 0x54)
print(f"Situations: count={sit_cnt}, arr=0x{sit_arr:X}")
for i in range(sit_cnt):
    sid = r32(sit_arr + i * 4)
    print(f"  Sit[{i}]: ID={sid}")

# 4. Points of Interest (+0x90, +0x9C)
poi_arr = r64(log_ptr + 0x90)
poi_cnt = r32(log_ptr + 0x9C)
print(f"POIs: count={poi_cnt}, arr=0x{poi_arr:X}")
for i in range(poi_cnt):
    elem = r64(poi_arr + i * 8)
    print(f"  POI[{i}]: 0x{elem:X}")

# 5. Anomalies at +0xF0
print(f"Anomalies struct at +0xF0 (0x{log_ptr + 0xF0:X}):")
for off in range(0xF0, 0x130, 8):
    v = r64(log_ptr + off)
    print(f"  +0x{off:02X}: 0x{v:016X}")
