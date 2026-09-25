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

fmgr = rp(base + 0x3113008)
f_arr = rp(fmgr + 0x18)
flt55 = rp(f_arr + (55 & 0xFFFFFF) * 16 + 8)

for off in range(0, 0x400, 8):
    begin = rp(flt55 + off)
    end = rp(flt55 + off + 8)
    cap = rp(flt55 + off + 16)
    if begin > 0x10000 and end >= begin and cap >= end and (end - begin) < 10000:
        sz_bytes = end - begin
        # check if 4-byte or 8-byte elements
        if 4 <= sz_bytes <= 400:
            cnt4 = sz_bytes // 4
            cnt8 = sz_bytes // 8
            print(f"+0x{off:X}: sz_bytes={sz_bytes} (cnt4={cnt4}, cnt8={cnt8}), begin=0x{begin:X}")
            # If cnt8 <= 10:
            if cnt8 > 0 and cnt8 <= 10:
                p0 = rp(begin)
                if p0 > 0x10000 and p0 < 0x7FFFFFFFFFFF:
                    # check p0 strings
                    s0 = read_pdx_string(p0 + 0xA8)
                    s1 = read_pdx_string(p0 + 0x20)
                    print(f"    elem0: 0x{p0:X}, s_A8='{s0}', s_20='{s1}'")
