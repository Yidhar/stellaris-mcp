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

bal_ptr = rp(player + 0x2B40)
stockpile_arr = rp(bal_ptr + 0x30)
inc_arr = rp(player + 0x2018)
exp_arr = rp(player + 0x2038)
net_arr = rp(player + 0x2058)

print(f"{'Resource':<22} | {'Stockpile':>10} | {'Income':>10} | {'Expense':>10} | {'True Net':>10}")
print("-" * 75)

for i in range(cnt):
    k = res_names[i]
    stk = ctypes.c_int64(); kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(stockpile_arr + i * 8), ctypes.byref(stk), 8, None)
    inc = ctypes.c_int64(); kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(inc_arr + i * 8), ctypes.byref(inc), 8, None)
    exp = ctypes.c_int64(); kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(exp_arr + i * 8), ctypes.byref(exp), 8, None)
    net = ctypes.c_int64(); kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(net_arr + i * 8), ctypes.byref(net), 8, None)
    
    stk_f = stk.value / 100000.0
    inc_f = inc.value / 100000.0
    exp_f = exp.value / 100000.0
    net_f = net.value / 100000.0
    
    if abs(stk_f) > 0.001 or abs(inc_f) > 0.001 or abs(exp_f) > 0.001 or abs(net_f) > 0.001:
        print(f"{k:<22} | {stk_f:>10.2f} | {inc_f:>10.2f} | {exp_f:>10.2f} | {net_f:>10.2f}")
