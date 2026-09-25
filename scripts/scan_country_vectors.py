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

cmgr = rp(base + 0x3112F50)
carr = rp(cmgr + 0x18)
player = rp(carr + 8)

# In CCountry, scan for vectors with count around 42 (colonies) or sectors (e.g. 1-10) or armies
for off in range(0, 0x4000, 8):
    # Vector layout: [ptr, cap, size] or [begin, end, cap]
    cnt = ru32(player + off + 0x14)
    ptr = rp(player + off + 8)
    if ptr > 0x10000 and ptr < 0x7FFFFFFFFFFF and 1 <= cnt <= 100:
        # Check first element
        elem0 = rp(ptr)
        print(f"Vector at player + 0x{off:X}: cnt={cnt}, ptr=0x{ptr:X}, elem0=0x{elem0:X} (u32={ru32(ptr)})")
