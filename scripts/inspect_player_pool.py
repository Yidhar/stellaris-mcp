import ctypes, struct

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory
pid = 104400
base = 0x7ff75ed50000
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read)):
        return struct.unpack('<I', buf.raw)[0]
    return 0

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

c_leader_mgr = 0x178BED036E0

# Inspect +0x28 (array of 13 items)
arr_ptr = r64(c_leader_mgr + 0x28)
count = r32(c_leader_mgr + 0x30)
cap = r32(c_leader_mgr + 0x34)
print(f"Array at +0x28: ptr=0x{arr_ptr:X}, count={count}, cap={cap}")

leader_mgr = r64(base + 0x3287320)
tbl = r64(leader_mgr + 0x18)
leader_cap = r32(leader_mgr + 0x20)

def get_leader(lid):
    slot = lid & 0xFFFF
    if slot < leader_cap:
        p = r64(tbl + slot * 16 + 8)
        if p and r32(p + 0x20) == lid:
            return p
    return None

for i in range(count):
    # Could each entry be uint32 leader_id (4 bytes) or uint64 (8 bytes) or 16 bytes?
    # Let's check 4 bytes:
    lid = r32(arr_ptr + i * 4)
    lp = get_leader(lid)
    name = ""
    cls = ""
    if lp:
        name = read_pdx_str(lp + 0x50)
        cls_p = r64(lp + 0xE0)
        if cls_p:
            cls = read_pdx_str(cls_p + 0x20)
    print(f"  Entry[{i}]: val32={lid} (0x{lid:X}) -> LeaderPtr=0x{lp or 0:X}, Name='{name}', Class='{cls}'")

# Inspect +0x40 (candidate pool?)
pool_ptr = r64(c_leader_mgr + 0x40)
pool_cnt = r32(c_leader_mgr + 0x48)
print(f"\nPool at +0x40: ptr=0x{pool_ptr:X}, count={pool_cnt}")
for i in range(pool_cnt):
    lid = r32(pool_ptr + i * 4)
    lp = get_leader(lid)
    name = ""
    cls = ""
    if lp:
        name = read_pdx_str(lp + 0x50)
        cls_p = r64(lp + 0xE0)
        if cls_p:
            cls = read_pdx_str(cls_p + 0x20)
    print(f"  Pool[{i}]: val32={lid} (0x{lid:X}) -> LeaderPtr=0x{lp or 0:X}, Name='{name}', Class='{cls}'")

