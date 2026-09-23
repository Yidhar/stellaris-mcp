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

print("Manager Offset | Active Count | Element Type / Sample Strings")
for off in range(0x32876A0, 0x32878E0, 8):
    mgr = r64(base + off)
    if not mgr: continue
    arr = r64(mgr + 0x18)
    cnt = r32(mgr + 0x20)
    if not arr or cnt == 0 or cnt > 50000: continue
    
    # count non-null elements
    active = 0
    sample_info = []
    for i in range(min(cnt, 2048)):
        elem = r64(arr + i * 16 + 8)
        if elem and elem > 0x10000:
            active += 1
            if len(sample_info) < 2:
                elem_vt = r64(elem)
                elem_vt_rva = elem_vt - base if base < elem_vt < base + 0x3000000 else 0
                # Scan first 0x80 bytes for strings
                s_list = []
                for s_off in range(0, 0x80, 8):
                    s = r_pdx_str(elem + s_off)
                    if s and len(s) >= 2 and not s.isdigit():
                        s_list.append(s)
                sample_info.append(f"elem[0x{elem:X}, vt=0x{elem_vt_rva:X}, strs={s_list}]")
    
    print(f"[+0x{off:X}] active={active:4d}/{cnt:<4d} : {'; '.join(sample_info)}")
