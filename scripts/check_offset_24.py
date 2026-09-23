import ctypes, struct

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory
pid = 104400
base = 0x7ff75ed50000
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read)):
        return struct.unpack('<I', buf.raw)[0]
    return 0

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

leader_mgr = r64(base + 0x3287320)
tbl = r64(leader_mgr + 0x18)
cap = r32(leader_mgr + 0x20)

vals_24 = set()
leaders_by_24 = {}

for i in range(cap):
    ptr = r64(tbl + i * 16 + 8)
    if ptr:
        lid = r32(ptr + 0x20)
        u24 = r32(ptr + 0x24)
        vals_24.add(u24)
        leaders_by_24.setdefault(u24, []).append((lid, ptr))

print(f"Distinct values at leader + 0x24: {sorted(vals_24)}")
for val in sorted(vals_24):
    print(f"  +0x24 == {val}: {len(leaders_by_24[val])} leaders (sample IDs: {[x[0] for x in leaders_by_24[val][:8]]})")

