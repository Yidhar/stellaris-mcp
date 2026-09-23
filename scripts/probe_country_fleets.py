import ctypes, struct
import inject
import reload_dll

pid = inject.find_stellaris_pid()
print(f"PID: {pid}")
base = reload_dll.find_module(pid, "stellaris.exe")
print(f"Base: {hex(base)}")

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

session_ptr = r64(base + 0x310D698)
country_mgr = r64(session_ptr + 0x580)
countries_arr = r64(country_mgr + 0x18)
player_country = r64(countries_arr + 8)
print(f"Player Country: {hex(player_country)}")

# Global fleet manager
fleet_mgr = r64(base + 0x3113008)
fleet_tbl = r64(fleet_mgr + 0x18)
fleet_cap = r32(fleet_mgr + 0x20)
print(f"Global Fleet Manager: {hex(fleet_mgr)}, cap: {fleet_cap}")

# Look at all fleets belonging to player
all_fleets = []
for i in range(fleet_cap):
    ptr = r64(fleet_tbl + i * 16 + 8)
    if ptr:
        fid = r32(ptr + 8)
        all_fleets.append((fid, ptr))

print(f"Total fleets in galaxy: {len(all_fleets)}")
fleet3_ptr = 0
for fid, ptr in all_fleets:
    if fid == 3:
        fleet3_ptr = ptr
        break

print(f"Fleet 3 Ptr: {hex(fleet3_ptr)}")
if fleet3_ptr:
    print("Dumping Fleet 3:")
    for off in range(0, 0x1A0, 8):
        u32_0 = r32(fleet3_ptr + off)
        u32_1 = r32(fleet3_ptr + off + 4)
        u64 = r64(fleet3_ptr + off)
        print(f"  +{hex(off)}: u64={hex(u64)} | u32=({u32_0}, {u32_1})")

# Also check template 0
ft_mgr = r64(base + 0x3113038)
ft_tbl = r64(ft_mgr + 0x18)
ft0_ptr = r64(ft_tbl + 8)
print(f"\nTemplate 0 Ptr: {hex(ft0_ptr)}")
if ft0_ptr:
    print("Dumping Template 0:")
    for off in range(0, 0xA0, 8):
        u32_0 = r32(ft0_ptr + off)
        u32_1 = r32(ft0_ptr + off + 4)
        u64 = r64(ft0_ptr + off)
        print(f"  +{hex(off)}: u64={hex(u64)} | u32=({u32_0}, {u32_1})")

    designs_arr = r64(ft0_ptr + 0x28)
    designs_cnt = r32(ft0_ptr + 0x34)
    print(f"\nDesigns Arr: {hex(designs_arr)}, Count: {designs_cnt}")
    if designs_arr:
        for off in range(0, 0x60, 4):
            val = r32(designs_arr + off)
            print(f"  design +{hex(off)}: {val} (0x{val:X})")
        print(f"  design +0x558: {r32(designs_arr + 0x558)}")
