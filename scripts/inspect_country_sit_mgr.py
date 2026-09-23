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

# Country pointer
mgr = r64(base + 0x3287788)
arr = r64(mgr + 0x18)
country = r64(arr + 8)
print(f"Player Country: 0x{country:X}")

sit_mgr = country + 0x1C98
print(f"CSituationManager at Country + 0x1C98: 0x{sit_mgr:X}")

# Let's inspect 0x100 bytes of CSituationManager
for off in range(0, 0x100, 8):
    v = r64(sit_mgr + off)
    # Check if this looks like a std::vector (start, end, capacity)
    if off % 24 == 0:
        v_start = r64(sit_mgr + off)
        v_end = r64(sit_mgr + off + 8)
        v_cap = r64(sit_mgr + off + 16)
        if v_start and v_end and v_cap and v_start <= v_end <= v_cap and (v_cap - v_start) % 8 == 0:
            count = (v_end - v_start) // 8
            print(f"  Vector at +0x{off:02X}: start=0x{v_start:X}, end=0x{v_end:X}, count={count}")
    print(f"  +0x{off:02X}: 0x{v:016X}")
