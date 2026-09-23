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

def extract_str(addr):
    buf = ctypes.create_string_buffer(32)
    read = ctypes.c_size_t()
    if not kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 32, ctypes.byref(read)):
        return ''
    raw = buf.raw
    sz = struct.unpack('<Q', raw[16:24])[0]
    cap = struct.unpack('<Q', raw[24:32])[0]
    if sz == 0 or sz > 500: return ''
    if cap < 16:
        return raw[:min(sz, 15)].decode('utf-8', errors='ignore')
    else:
        ptr = struct.unpack('<Q', raw[:8])[0]
        if ptr and 0x10000 < ptr < 0x7FFFFFFFFFFF:
            sbuf = ctypes.create_string_buffer(min(sz, 256))
            if kernel32.ReadProcessMemory(h, ctypes.c_void_p(ptr), sbuf, min(sz, 256), ctypes.byref(read)):
                return sbuf.raw[:read.value].decode('utf-8', errors='ignore')
    return ''

mgr = r64(base + 0x32877E0)
arr = r64(mgr + 0x18)
sit = r64(arr + 1 * 16 + 8) # Situation 1

type_ptr = 0x20AA5D32A10
print(f'Scanning CSituationType at 0x{type_ptr:X}:')
for off in range(0, 0x400, 8):
    q = r64(type_ptr + off)
    s = extract_str(type_ptr + off)
    if s:
        print(f'  +0x{off:03X}: str="{s}"')
    elif q and 0x10000 < q < 0x7FFFFFFFFFFF:
        sq = extract_str(q)
        if sq:
            print(f'  +0x{off:03X}: -> str="{sq}"')




