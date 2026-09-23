import ctypes, struct, io, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

kernel32 = ctypes.WinDLL('kernel32')
h = kernel32.OpenProcess(0x10, False, 88688)
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
    return struct.unpack('<i', buf.raw)[0]

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

c0 = 0x1D9E0036040
tech_mgr = c0 + 0x1870

areas = ["Physics", "Society", "Engineering"]
for area_idx in range(3):
    r12 = tech_mgr + (area_idx + 11) * 24
    arr_ptr = r64(r12 + 8)
    count = r32(r12 + 0x14)
    print(f"\n=== Area {area_idx}: {areas[area_idx]} (r12=0x{r12:X}) ===")
    print(f"Count: {count}, Array Ptr: 0x{arr_ptr:X}")
    
    if arr_ptr and count > 0:
        for i in range(count):
            tech_ptr = r64(arr_ptr + i * 8)
            # Inspect tech_ptr fields
            # Let's search for string key in tech_ptr
            key = ""
            for off in range(0, 0x80, 8):
                s = r_pdx_str(tech_ptr + off)
                if s and len(s) > 2 and ("tech_" in s or "physics" in s or "society" in s or "engineering" in s or s.isidentifier()):
                    key = s
                    break
            print(f"  Card [{i}]: tech_ptr=0x{tech_ptr:X}, detected key='{key}'")
            # Print all non-empty strings in tech_ptr
            for off in range(0, 0x100, 8):
                s = r_pdx_str(tech_ptr + off)
                if s:
                    print(f"    +0x{off:02X}: pdx_str='{s}'")
