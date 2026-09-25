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

# 0xACC68A: mov rdx, qword ptr [rip + 0x2646827] -> next rip = base + 0xACC691
m1 = base + 0xACC691 + 0x2646827
print(f"m1: 0x{m1 - base:X} -> 0x{read_u64(m1):X}")

# 0xACC6E0: mov rdx, qword ptr [rip + 0x2646869] -> next rip = base + 0xACC6E7
m2 = base + 0xACC6E7 + 0x2646869
print(f"m2: 0x{m2 - base:X} -> 0x{read_u64(m2):X}")
