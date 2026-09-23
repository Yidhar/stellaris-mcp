import ctypes, struct

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory
pid = 104400
base = 0x7ff75ed50000
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read)):
        return struct.unpack('<I', buf.raw)[0]
    return 0

def read_pdx_str(addr):
    cap = r64(addr + 0x18)
    size = r64(addr + 0x10)
    if size == 0 or size > 1024: return ""
    buf = ctypes.create_string_buffer(min(size, 256))
    read = ctypes.c_size_t()
    if cap < 16:
        ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, min(size, 15), ctypes.byref(read))
        return buf.raw[:read.value].decode('utf-8', errors='ignore')
    else:
        ptr = r64(addr)
        if ptr:
            ReadProcessMemory(hProc, ctypes.c_void_p(ptr), buf, min(size, 256), ctypes.byref(read))
            return buf.raw[:read.value].decode('utf-8', errors='ignore')
    return ""

elem = 0x1798349D750
print(f"Dumping 0x{elem:X}:")
buf = ctypes.create_string_buffer(128)
read = ctypes.c_size_t()
ReadProcessMemory(hProc, ctypes.c_void_p(elem), buf, 128, ctypes.byref(read))
raw = buf.raw

for off in range(0, 128, 8):
    q = struct.unpack('<Q', raw[off:off+8])[0]
    s = read_pdx_str(elem + off)
    s_ptr = ""
    if 0x17000000000 <= q <= 0x18000000000:
        s_ptr = read_pdx_str(q + 0x20) or read_pdx_str(q)
    print(f"  +{hex(off)}: 0x{q:016X} | str='{s}' | ptr->str='{s_ptr}'")

