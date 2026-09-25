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

flt55 = 0x2B460273610 # Fleet 55

# Scan vectors in flt55
for off in range(0, 0x400, 8):
    ptr = rp(flt55 + off + 8)
    cnt = ru32(flt55 + off + 0x14)
    if ptr > 0x10000 and ptr < 0x7FFFFFFFFFFF and 1 <= cnt <= 50:
        elem0 = rp(ptr)
        print(f"+0x{off:X}: cnt={cnt}, ptr=0x{ptr:X}, elem0=0x{elem0:X}")
        # If elem0 is a ship pointer or ship id
        for i in range(cnt):
            e = rp(ptr + i * 8)
            # check strings in e
            if e > 0x10000 and e < 0x7FFFFFFFFFFF:
                for s_off in range(0, 0x100, 8):
                    s = read_pdx_string(e + s_off)
                    if s and len(s) > 2 and any(c.isalnum() for c in s):
                        print(f"    elem[{i}] + 0x{s_off:X}: '{s}'")
