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

ft_mgr = r64(base + 0x3113038)
ft_arr = r64(ft_mgr + 0x18)
ft0 = r64(ft_arr + 8)

# Check all templates in ft_mgr
cap = r32(ft_mgr + 0x20)
print(f"Template manager cap: {cap}")
for i in range(cap):
    p = r64(ft_arr + i * 16 + 8)
    if p:
        tid = r32(p + 8)
        u70 = r32(p + 0x70)
        u78 = r32(p + 0x78)
        u88 = r32(p + 0x88)
        u90 = r32(p + 0x90)
        print(f"Template [{i}]: ptr={hex(p)}, tid={tid}, +0x70={u70}, +0x78={u78}, +0x88={u88}, +0x90={u90}")
