import ctypes, struct

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

def r64(addr):
    raw = read_bytes(addr, 8)
    return struct.unpack('<Q', raw)[0] if raw else 0

def r32(addr):
    raw = read_bytes(addr, 4)
    return struct.unpack('<I', raw)[0] if raw else 0

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

ruler_ptr = 0x179df9eb378

print(f"Dumping ruler (0x{ruler_ptr:X}) from 0x300 to 0x800:")
for off in range(0x300, 0x800, 8):
    q = r64(ruler_ptr + off)
    u0 = r32(ruler_ptr + off)
    u1 = r32(ruler_ptr + off + 4)
    
    # Check if q points to string or object
    s = read_pdx_str(ruler_ptr + off)
    s_ptr = ""
    if 0x17000000000 <= q <= 0x18000000000:
        s_ptr = read_pdx_str(q + 0x20) or read_pdx_str(q)
    
    extra = ""
    if s: extra += f" [str: '{s}']"
    if s_ptr: extra += f" [ptr->str: '{s_ptr}']"
    
    if q != 0 or extra:
        print(f"  +{hex(off):<5}: 0x{q:016X} | u0={u0:<10}, u1={u1:<10}{extra}")

