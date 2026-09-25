import sys, os, ctypes
from ctypes import wintypes
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

def read_pdx_string(addr):
    cap = read_ptr(addr + 24)
    sz = read_ptr(addr + 16)
    if sz == 0 or sz > 512: return ""
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = read_ptr(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

idler = read_ptr(base + 0x3113180)
mview = read_ptr(idler + 0xDC0)
print(f"Idler: 0x{idler:X}, MView: 0x{mview:X}")

# Let's inspect Country
cmgr = read_ptr(base + 0x3112F50)
carr = read_ptr(cmgr + 0x18)
player = read_ptr(carr + 8)
print(f"Player country: 0x{player:X}")

# Check resources in resource DB
res_db = read_ptr(base + 0x3150E78)
cnt = read_u32(res_db + 0x14)
arr = read_ptr(res_db + 0x08)
print(f"Resource DB count: {cnt}")

for i in range(cnt):
    r_ptr = read_ptr(arr + i * 8)
    if not r_ptr: continue
    k = read_pdx_string(r_ptr + 0x30)
    base_amt = read_ptr(r_ptr + 0x158)
    base_price = read_ptr(r_ptr + 0x160)
    if base_amt > 0 and base_price > 0:
        print(f"Tradable candidate: {k} (amt={base_amt/100000.0}, price={base_price/100000.0})")
