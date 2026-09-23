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

ft_mgr = r64(base + 0x3113038)
ft_arr = r64(ft_mgr + 0x18)
ft0 = r64(ft_arr + 8)
d_arr = r64(ft0 + 0x28)

print(f"Designs array: {hex(d_arr)}")
for off in range(0, 0x560, 4):
    val = r32(d_arr + off)
    if val != 0:
        print(f"  +{hex(off)}: {val} (0x{val:X})")
