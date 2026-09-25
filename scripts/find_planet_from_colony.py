import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def rp(a):
    v = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 8, None)
    return v.value

def ru32(a):
    v = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 4, None)
    return v.value

def read_pdx_string(addr):
    cap = rp(addr + 24)
    sz = rp(addr + 16)
    if sz == 0 or sz > 512: return ""
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = rp(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

earth = 0x2B4633F38D0

# In CColony, where is planet pointer?
# Let's inspect all pointers in earth
for off in range(0, 0x400, 8):
    p = rp(earth + off)
    if p > 0x10000 and p < 0x7FFFFFFFFFFF:
        # Check if p has pc_ or NAME_
        for sub_off in range(0, 0x200, 8):
            s = read_pdx_string(p + sub_off)
            if s and (s.startswith("pc_") or s.startswith("NAME_") or "Earth" in s):
                print(f"earth + 0x{off:X} (0x{p:X}) -> sub+0x{sub_off:X}: '{s}'")
