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

fleet_mgr = r64(base + 0x3113008)
fleet_tbl = r64(fleet_mgr + 0x18)
fleet3_ptr = r64(fleet_tbl + 3 * 16 + 8)

print(f"Fleet 3 Ptr: {hex(fleet3_ptr)}")
# Check all fields of Fleet 3 up to 0x300
for off in range(0, 0x300, 8):
    p = r64(fleet3_ptr + off)
    u0 = r32(fleet3_ptr + off)
    u1 = r32(fleet3_ptr + off + 4)
    if u0 == 3 or u1 == 3:
        print(f"Match 3 at +{hex(off)}: u0={u0}, u1={u1}, p={hex(p)}")
    # If p looks like a vector (ptr, cap, size)
    if p > 0x10000 and p < 0x7FFFFFFFFFFF:
        sz = r32(fleet3_ptr + off + 0x10)
        sz2 = r32(fleet3_ptr + off + 0xC)
        if sz == 3 or sz2 == 3:
            print(f"Possible ship vector at +{hex(off)}: sz={sz}, sz2={sz2}, p={hex(p)}")

# Also let's check CShipManager
# In Stellaris, is there CShipManager?
# Let's search around base + 0x3113000
for mgr_off in range(0x3113000, 0x3113080, 8):
    m = r64(base + mgr_off)
    if m:
        cap = r32(m + 0x20)
        cnt = r32(m + 0x24)
        print(f"Manager at base+{hex(mgr_off)}: ptr={hex(m)}, cap={cap}, cnt={cnt}")
