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
fleet_arr = r64(fleet_mgr + 0x18)

p3 = r64(fleet_arr + 3 * 16 + 8)
p37 = r64(fleet_arr + 37 * 16 + 8)
p94 = r64(fleet_arr + 94 * 16 + 8)

print("Offset | Fleet 3 (Military) | Fleet 37 (Science) | Fleet 94 (Constructor)")
print("-" * 75)
for off in range(0, 0x190, 8):
    v3 = r64(p3 + off)
    v37 = r64(p37 + off)
    v94 = r64(p94 + off)
    if v3 != v37 or v3 != v94:
        print(f"+{hex(off):5s} | {hex(v3):18s} | {hex(v37):18s} | {hex(v94):18s}")
