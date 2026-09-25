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

# Search for pc_ in earth and all its sub-pointers
for off in range(0, 0x600, 8):
    p = rp(earth + off)
    s = read_pdx_string(earth + off)
    if "pc_" in s:
        print(f"Direct match at +0x{off:X}: '{s}'")
    if p > 0x10000 and p < 0x7FFFFFFFFFFF:
        s_ptr = read_pdx_string(p)
        if "pc_" in s_ptr:
            print(f"Pointer match at +0x{off:X} -> '{s_ptr}'")
        s_ptr20 = read_pdx_string(p + 0x20)
        if "pc_" in s_ptr20:
            print(f"Pointer match at +0x{off:X} (+0x20) -> '{s_ptr20}'")

