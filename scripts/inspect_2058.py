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

res_db = rp(base + 0x3150E78)
cnt = ru32(res_db + 0x14)
arr = rp(res_db + 8)

res_names = []
for i in range(cnt):
    res_ptr = rp(arr + i * 8)
    k = read_pdx_string(res_ptr + 0x30)
    res_names.append(k)

print(f"Total resources: {cnt}")
print(f"Resource at index 8: '{res_names[8]}'")

p_2058 = rp(player + 0x2058)
print(f"player + 0x2058: 0x{p_2058:X}")

for i in range(cnt):
    raw_val = ctypes.c_int64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(p_2058 + i * 8), ctypes.byref(raw_val), 8, None)
    val = raw_val.value / 100000.0
    print(f"  [{i}] {res_names[i]}: {val:.4f} (raw={raw_val.value})")
