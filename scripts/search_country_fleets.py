import ctypes, struct
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read)):
        return struct.unpack('<I', buf.raw)[0]
    return 0

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

mgr = r64(base + 0x3112F50)
arr = r64(mgr + 0x18)
player_country = r64(arr + 8)

print(f"Player Country: {hex(player_country)}")

# Scan player_country for any vector containing fleet ID 3
# In Stellaris, the player starts with:
# - Corvette fleet (fleet_id = 3)
# - Science ship
# - Construction ship
# - Starbase fleet?
# Let's search all vectors in player_country
for off in range(0, 0x4000, 8):
    v_ptr = r64(player_country + off + 8)
    v_cap = r32(player_country + off + 16)
    v_cnt = r32(player_country + off + 20)
    if v_ptr > 0x10000 and v_ptr < 0x7FFFFFFFFFFF and 0 < v_cnt <= 50 and v_cap >= v_cnt:
        # Read elements
        elems = [r32(v_ptr + k * 4) for k in range(v_cnt)]
        if 3 in elems:
            print(f"Vector at country+{hex(off)}: cnt={v_cnt}, cap={v_cap}, elems={elems}")
