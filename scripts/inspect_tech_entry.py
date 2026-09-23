import ctypes, struct

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
        if ptr and ptr > 0x10000 and ptr < 0x7FFFFFFFFFFF:
            buf = ctypes.create_string_buffer(min(size, 256))
            read = ctypes.c_size_t()
            kernel32.ReadProcessMemory(h, ctypes.c_void_p(ptr), buf, min(size, 256), ctypes.byref(read))
            return buf.raw[:read.value].decode('utf-8', errors='ignore')
    return ""

def r_cstr(addr, maxlen=64):
    if not addr or addr < 0x10000 or addr > 0x7FFFFFFFFFFF:
        return ""
    buf = ctypes.create_string_buffer(maxlen)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, maxlen, ctypes.byref(read))
    return buf.raw.split(b'\x00')[0].decode('utf-8', errors='ignore')

entry = 0x1DA21C87C80
print(f"entry 0: 0x{entry:X}, vtable: 0x{r64(entry):X} (RVA 0x{r64(entry) - base:X})")

# Look at fields of entry
for off in range(0, 0x150, 8):
    v = r64(entry + off)
    s_pdx = r_pdx_str(entry + off)
    s_c = r_cstr(v)
    info = ""
    if s_pdx:
        info += f" [pdx: '{s_pdx}']"
    if s_c and len(s_c) > 2:
        info += f" [cstr: '{s_c}']"
    print(f"  +0x{off:03X}: 0x{v:016X}{info}")
