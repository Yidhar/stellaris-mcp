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

# Check candidate sector vectors:
for off in [0x2540, 0x2558, 0x2588, 0x2778, 0x27C0, 0x2880, 0x36F0, 0x3990, 0x39F0, 0x3A18, 0x3A50]:
    cnt = ru32(player + off + 0x14)
    ptr = rp(player + off + 8)
    print(f"=== Vector at player + 0x{off:X} (cnt={cnt}) ===")
    for i in range(cnt):
        elem = rp(ptr + i * 8)
        print(f"  [{i}]: 0x{elem:X}")
        if elem > 0x10000 and elem < 0x7FFFFFFFFFFF:
            for s_off in range(0, 0x100, 8):
                s = read_pdx_string(elem + s_off)
                if s and len(s) > 1 and any(c.isalnum() for c in s):
                    print(f"       +0x{s_off:X}: '{s}'")
