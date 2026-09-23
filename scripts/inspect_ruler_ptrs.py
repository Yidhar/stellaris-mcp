import ctypes
import struct

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory
pid = 104400
base = 0x7ff75ed50000
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    bytesRead = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(bytesRead)):
        return buf.raw
    return None

def read_u32(addr):
    raw = read_bytes(addr, 4)
    return struct.unpack("<I", raw)[0] if raw else 0

def read_u64(addr):
    raw = read_bytes(addr, 8)
    return struct.unpack("<Q", raw)[0] if raw else 0

def read_pdx_string(addr):
    cap = read_u64(addr + 0x18)
    size = read_u64(addr + 0x10)
    if size == 0 or size > 1024:
        return ""
    if cap < 16:
        raw = read_bytes(addr, size)
        return raw.decode('utf-8', errors='ignore') if raw else ""
    else:
        ptr = read_u64(addr)
        if ptr:
            raw = read_bytes(ptr, size)
            return raw.decode('utf-8', errors='ignore') if raw else ""
    return ""

ruler_ptr = 0x179df9eb378

# Inspect all pointers in ruler
for off in range(0, 0x400, 8):
    p = read_u64(ruler_ptr + off)
    if 0x17000000000 <= p <= 0x18000000000:
        # Heap pointer! What's there?
        first_qword = read_u64(p)
        first_u32 = read_u32(p)
        s = read_pdx_string(p)
        vt = read_u64(p)
        print(f"Ruler + {hex(off)}: -> {hex(p)} (u64_0={hex(first_qword)}, u32_0={first_u32}, str='{s}')")

