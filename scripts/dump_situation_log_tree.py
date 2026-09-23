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

def r8(addr):
    buf = ctypes.create_string_buffer(1)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 1, ctypes.byref(read))
    return struct.unpack('<B', buf.raw)[0]

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
        if ptr and ptr > 0x10000:
            buf = ctypes.create_string_buffer(min(size, 256))
            read = ctypes.c_size_t()
            kernel32.ReadProcessMemory(h, ctypes.c_void_p(ptr), buf, min(size, 256), ctypes.byref(read))
            return buf.raw[:read.value].decode('utf-8', errors='ignore')
    return ""

def dump_ui(node, depth=0, max_depth=3):
    if not node or depth > max_depth:
        return
    name = r_pdx_str(node + 0x18)
    vt = r64(node)
    vis = r8(node + 0x41)
    rva = vt - base if vt > base else 0
    print("  " * depth + f"- [0x{node:X}] name='{name}' vis={vis} RVA=0x{rva:X}")
    ctrls = r64(node + 0x668)
    cnt = r32(node + 0x670)
    if ctrls and 0 < cnt < 100:
        for i in range(cnt):
            child = r64(ctrls + i * 8)
            dump_ui(child, depth + 1, max_depth)

idler = r64(base + 0x3287900)
sit_view = r64(idler + 0xD30)
win = r64(sit_view + 0x78)
print(f"SituationLog Window: 0x{win:X}")
dump_ui(win, 0, 3)
