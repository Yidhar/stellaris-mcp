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

# Let's inspect base + 0x3285B18
c_ptr = r64(base + 0x3285B18)
print(f"base + 0x3285B18: 0x{c_ptr:X}")

# Check country name or properties of c_ptr
# In CCountry, where is name or id?
# Let's check first 0x100 bytes of c_ptr
for off in range(0, 0x100, 8):
    val = r64(c_ptr + off)
    print(f"  +0x{off:02X}: 0x{val:016X}")
