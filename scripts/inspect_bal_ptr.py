import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def read_ptr(addr):
    val = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 8, None)
    return val.value

def read_u32(addr):
    val = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 4, None)
    return val.value

cmgr = read_ptr(base + 0x3112F50)
carr = read_ptr(cmgr + 0x18)
player = read_ptr(carr + 8)

bal_ptr = read_ptr(player + 0x2B40)
print(f"bal_ptr: 0x{bal_ptr:X}")

for off in range(0, 0x100, 8):
    v = read_ptr(bal_ptr + off)
    print(f"  +0x{off:X}: 0x{v:X}")
