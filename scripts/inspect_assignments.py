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

def read_u32(addr):
    raw = read_bytes(addr, 4)
    return struct.unpack("<I", raw)[0] if raw else 0

def read_u64(addr):
    raw = read_bytes(addr, 8)
    return struct.unpack("<Q", raw)[0] if raw else 0

def read_pdx_str(addr):
    cap = read_u64(addr + 0x18)
    size = read_u64(addr + 0x10)
    if size == 0 or size > 1024: return ""
    if cap < 16:
        raw = read_bytes(addr, size)
        return raw.decode('utf-8', errors='ignore') if raw else ""
    else:
        ptr = read_u64(addr)
        if ptr:
            raw = read_bytes(ptr, size)
            return raw.decode('utf-8', errors='ignore') if raw else ""
    return ""

leader_mgr = read_u64(base + 0x3287320)
tbl = read_u64(leader_mgr + 0x18)
cap = read_u32(leader_mgr + 0x20)

for i in range(cap):
    ptr = read_u64(tbl + i * 16 + 8)
    if ptr:
        lid = read_u32(ptr + 0x20)
        name_key = read_pdx_str(ptr + 0x50)
        cls_ptr = read_u64(ptr + 0xE0)
        cls_key = read_pdx_str(cls_ptr + 0x20) if cls_ptr else ""
        
        # Read +0x110 to +0x130
        assign_type = read_u32(ptr + 0x110) & 0xFF
        assign_sub = read_u32(ptr + 0x114)
        assign_target = read_u32(ptr + 0x118)
        
        # Check if assign_type != 0 or target != 0xFFFFFFFF
        if assign_target != 0xFFFFFFFF or assign_type != 0:
            print(f"Leader ID={lid:10d}, Name='{name_key[:20]:20s}', Class='{cls_key:10s}': Type={assign_type}, Sub={assign_sub}, Target={assign_target}")

