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

def r8(addr):
    buf = ctypes.create_string_buffer(1)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 1, ctypes.byref(read))
    return struct.unpack('<B', buf.raw)[0]

def r_pdx_str(addr):
    cap = r64(addr + 0x10)
    size = r64(addr + 0x8)
    if size == 0:
        return ""
    if cap < 16:
        buf = ctypes.create_string_buffer(16)
        read = ctypes.c_size_t()
        kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, min(size, 15), ctypes.byref(read))
        return buf.raw[:read.value].decode('utf-8', errors='ignore')
    else:
        ptr = r64(addr)
        if ptr:
            buf = ctypes.create_string_buffer(min(size, 256))
            read = ctypes.c_size_t()
            kernel32.ReadProcessMemory(h, ctypes.c_void_p(ptr), buf, min(size, 256), ctypes.byref(read))
            return buf.raw[:read.value].decode('utf-8', errors='ignore')
    return ""

select_win = 0x1DA21E2B5B0
print(f"select_win: 0x{select_win:X}, vtable: 0x{r64(select_win):X}")

# Check child controls at +0x668
ctrls_vec = r64(select_win + 0x668)
ctrls_cnt = r32(select_win + 0x670)
print(f"ctrls_vec: 0x{ctrls_vec:X}, count: {ctrls_cnt}")

if ctrls_vec and ctrls_cnt > 0:
    for i in range(min(ctrls_cnt, 30)):
        ctrl = r64(ctrls_vec + i * 8)
        vt = r64(ctrl)
        vis = r8(ctrl + 0x41)
        name = r_pdx_str(ctrl + 0x18)
        print(f"  [{i:2d}] ctrl: 0x{ctrl:X}, vis={vis}, name='{name}'")
