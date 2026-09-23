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

mgr = r64(base + 0x3287788)
arr = r64(mgr + 0x18)
country = r64(arr + 8)

offsets = [
    0x1A08, 0x1A60, 0x1AB8, 0x1B10, 0x1B68, 0x1B98, 0x1BB0, 0x1BC8, 0x1BE0,
    0x1C50, 0x1C78, 0x1C90, 0x1C98, 0x1CE8, 0x1D88, 0x1DE8, 0x1E00, 0x1EA0
]

for off in offsets:
    addr = country + off
    vt = r64(addr)
    vt_rva = vt - base if base < vt < base + 0x3000000 else 0
    str_val = r_pdx_str(addr + 8)
    # Check if has vector at +8 or +0x10
    v0 = r64(addr + 8)
    v1 = r64(addr + 0x10)
    v2 = r64(addr + 0x18)
    v_info = ""
    if v0 and v1 and v2 and 0x10000 < v0 <= v1 <= v2 and (v1 - v0) % 8 == 0:
        v_info = f" vec_cnt={(v1 - v0)//8}"
    # Check if vector at +0x10, +0x18, +0x20
    v3 = r64(addr + 0x20)
    if v1 and v2 and v3 and 0x10000 < v1 <= v2 <= v3 and (v2 - v1) % 8 == 0:
        v_info += f" vec2_cnt={(v2 - v1)//8}"
    print(f"+0x{off:04X}: vt=0x{vt_rva:X} str='{str_val}'{v_info}")
