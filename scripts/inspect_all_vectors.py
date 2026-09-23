import ctypes, struct, io, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

kernel32 = ctypes.WinDLL('kernel32')
h = kernel32.OpenProcess(0x10, False, 88688)

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read))
    return struct.unpack('<Q', buf.raw)[0]

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read))
    return struct.unpack('<i', buf.raw)[0]

def read_pdx_string(addr):
    size = r64(addr + 0x10)
    cap = r64(addr + 0x18)
    if size == 0 or size > 4096:
        return ""
    if cap < 16:
        buf = ctypes.create_string_buffer(16)
        read = ctypes.c_size_t()
        kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, min(size, 15), ctypes.byref(read))
        return buf.raw[:read.value].decode('utf-8', errors='ignore')
    else:
        heap_ptr = r64(addr)
        if heap_ptr and 0x10000 < heap_ptr < 0x7FFFFFFFFFFF:
            buf = ctypes.create_string_buffer(min(size, 256))
            read = ctypes.c_size_t()
            kernel32.ReadProcessMemory(h, ctypes.c_void_p(heap_ptr), buf, min(size, 256), ctypes.byref(read))
            return buf.raw[:read.value].decode('utf-8', errors='ignore')
    return ""

c0 = 0x1D9E0036040
tech_mgr = c0 + 0x1870

print("--- Inspecting all vectors in tech_mgr (stride 24 = 0x18) ---")
for i in range(20):
    vec_addr = tech_mgr + i * 24
    p0 = r64(vec_addr)
    p1 = r64(vec_addr + 8)
    p2 = r64(vec_addr + 0x10)
    cnt = r32(vec_addr + 0x14)
    print(f"[{i:2d}] +0x{i*24:03X}: +0=0x{p0:016X}, +8=0x{p1:016X}, +16=0x{p2:016X}, cnt={cnt}")
