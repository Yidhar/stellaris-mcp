import ctypes, struct, io, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

kernel32 = ctypes.WinDLL('kernel32')
h = kernel32.OpenProcess(0x10, False, 88688)

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read))
    return struct.unpack('<Q', buf.raw)[0]

def read_pdx_string(addr):
    size = r64(addr + 0x10)
    cap = r64(addr + 0x18)
    if size == 0 or size > 4096: return ''
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
    return ''

tech = 0x1D97FCC9620
print(f"Strings in tech 0x{tech:X}:")
for off in range(0, 0x400, 8):
    s = read_pdx_string(tech + off)
    if s and len(s) > 1:
        print(f"  +0x{off:03X}: '{s}'")
