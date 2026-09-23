import ctypes, struct, sys
sys.stdout.reconfigure(encoding='utf-8')

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory
pid = 104400
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_pdx_str(addr):
    buf = ctypes.create_string_buffer(64)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 64, ctypes.byref(read)):
        raw = buf.raw
        size = struct.unpack('<Q', raw[16:24])[0]
        cap = struct.unpack('<Q', raw[24:32])[0]
        if 0 < size < 200:
            if cap < 16:
                return raw[:size].decode('utf-8', errors='replace')
            else:
                ptr = struct.unpack('<Q', raw[:8])[0]
                if ptr and 0x10000 < ptr < 0x7FFFFFFFFFFF:
                    sbuf = ctypes.create_string_buffer(size)
                    if ReadProcessMemory(hProc, ctypes.c_void_p(ptr), sbuf, size, ctypes.byref(read)):
                        return sbuf.raw[:read.value].decode('utf-8', errors='replace')
    return ""

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read))
    return struct.unpack('<Q', buf.raw)[0]

ptr = 0x17838BC0010
print(f"Dumping 0x{ptr:X}:")
for off in range(0, 0x100, 8):
    q = r64(ptr + off)
    s = read_pdx_str(ptr + off)
    s_ptr = ""
    if 0x17000000000 <= q <= 0x18000000000:
        s_ptr = read_pdx_str(q + 0x20) or read_pdx_str(q)
    print(f"  +{hex(off)}: 0x{q:016X} s='{s}' s_ptr='{s_ptr}'")

