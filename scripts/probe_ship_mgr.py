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

mgr = r64(base + 0x3112FB0)
arr = r64(mgr + 0x18)
cap = r32(mgr + 0x20)
print(f"Manager 0x3112FB0: arr={hex(arr)}, cap={cap}")

# Look for ships belonging to fleet 3
ships_fleet3 = []
for i in range(cap):
    p = r64(arr + i * 16 + 8)
    if p:
        # Check if any field in first 0x80 bytes equals 3 (fleet_id)
        for off in range(0, 0x80, 4):
            val = r32(p + off)
            if val == 3:
                # check if this is fleet_id
                ships_fleet3.append((i, hex(p), hex(off)))
                break

print(f"Items with 3 in first 0x80 bytes: {len(ships_fleet3)}")
for item in ships_fleet3[:10]:
    print(" ", item)
