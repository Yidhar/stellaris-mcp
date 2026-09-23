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

def r_str(addr, maxlen=64):
    buf = ctypes.create_string_buffer(maxlen)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, maxlen, ctypes.byref(read))
    return buf.raw.split(b'\x00')[0].decode('latin-1', errors='ignore')

idler = r64(base + 0x3287900)
tech_view = r64(idler + 0xD48)
print(f"tech_view: 0x{tech_view:X}")

# Let's inspect sub-windows or fields of tech_view
# In CTechnologyView ctor:
# +0x98 = 3
# +0xa0 = rdx (idler)
# +0xd0 = vtable
for off in range(0x90, 0x180, 8):
    v = r64(tech_view + off)
    print(f"  +0x{off:03X}: 0x{v:016X}")
