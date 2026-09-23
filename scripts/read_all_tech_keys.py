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
    # addr is address of CPdxString
    # +0x00: buf[16] (or heap_ptr at +0x00)
    # +0x10: size (8 bytes)
    # +0x18: capacity (8 bytes)
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

areas = ["Physics (物理学)", "Society (社会学)", "Engineering (工程学)"]
for area_idx in range(3):
    r12 = tech_mgr + (area_idx + 11) * 24
    arr_ptr = r64(r12 + 8)
    count = r32(r12 + 0x14)
    print(f"\n==========================================")
    print(f" Area {area_idx}: {areas[area_idx]} (Cards Available: {count})")
    print(f"==========================================")
    
    if arr_ptr and count > 0:
        for i in range(count):
            tech_ptr = r64(arr_ptr + i * 8)
            tech_key = read_pdx_string(tech_ptr + 0x20)
            print(f"  [{i}] tech_ptr=0x{tech_ptr:X} -> key: '{tech_key}'")
