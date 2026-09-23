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

country_mgr = r64(base + 0x3287788)
arr = r64(country_mgr + 0x18)
player_country = r64(arr + 8)

print(f"Player Country: 0x{player_country:X}")

c_leader_mgr = r64(player_country + 0x2CB0)
print(f"Country Leader Manager [Country + 0x2CB0]: 0x{c_leader_mgr:X}")

if c_leader_mgr:
    vt = r64(c_leader_mgr)
    print(f"  vtable: 0x{vt:X} (RVA 0x{vt - base:X})")
    for off in range(0, 0x100, 8):
        q = r64(c_leader_mgr + off)
        u0 = r32(c_leader_mgr + off)
        u1 = r32(c_leader_mgr + off + 4)
        print(f"  +{hex(off)}: 0x{q:016X} | u0={u0}, u1={u1}")

