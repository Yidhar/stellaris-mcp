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

c_leader_mgr = 0x178BED036E0

print("Dumping c_leader_mgr (+0x00 to +0x300):")
for off in range(0, 0x300, 8):
    q = r64(c_leader_mgr + off)
    u0 = r32(c_leader_mgr + off)
    u1 = r32(c_leader_mgr + off + 4)
    info = ""
    if 0x17000000000 <= q <= 0x18000000000:
        info = f" -> Heap 0x{q:X}"
    elif base < q < base + 0x3000000:
        info = f" -> Code/Rdata RVA 0x{q - base:X}"
    print(f"  +{hex(off):<6}: 0x{q:016X} | u0={u0:<10}, u1={u1:<10}{info}")

