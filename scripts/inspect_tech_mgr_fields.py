import ctypes
import struct

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

ptr = r64(base + 0x3285B18)
tech_mgr = ptr + 0x1870
print(f"tech_mgr: 0x{tech_mgr:X}")

for off in range(0, 0x180, 8):
    v64 = r64(tech_mgr + off)
    v32_0 = r32(tech_mgr + off)
    v32_1 = r32(tech_mgr + off + 4)
    print(f"  +0x{off:03X}: 0x{v64:016X} | u32: {v32_0}, {v32_1}")
