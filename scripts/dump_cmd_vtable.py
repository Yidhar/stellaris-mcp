import ctypes
import win32process
import win32api

pid = 77180
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_u64(addr):
    buf = ctypes.c_uint64()
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), ctypes.byref(buf), 8, ctypes.byref(read))
    return buf.value

vt = base + 0x23C09F8
print(f"Command vtable: 0x{vt - base:X}")
for i in range(16):
    fn = read_u64(vt + i * 8)
    print(f"  vfunc[{i}]: 0x{fn - base:X}")
