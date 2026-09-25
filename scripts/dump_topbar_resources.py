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

idler = rp(base + 0x3113180)
ui_win = rp(idler + 0x78)
c_arr = rp(ui_win + 0x878)
c_cnt = ru32(ui_win + 0x884)

shared_tb = None
for i in range(c_cnt):
    child = rp(c_arr + i * 8)
    v_names = rp(child + 0x890)
    v_cnt = ru32(child + 0x89C)
    if v_names and v_cnt > 0:
        first_name = read_pdx_string(v_names + 16)
        if first_name == "tb_energy_group":
            shared_tb = child
            break

if not shared_tb:
    print("shared_tb not found")
    exit(0)

print(f"Found shared_tb: 0x{shared_tb:X}")
stb_c_arr = rp(shared_tb + 0x878)
stb_cnt = ru32(shared_tb + 0x884)
vec_names = rp(shared_tb + 0x890)
names_cnt = ru32(shared_tb + 0x89C)

limit = min(stb_cnt, names_cnt)
for i in range(limit):
    gname = read_pdx_string(vec_names + i * 48 + 16)
    group_child = rp(stb_c_arr + i * 8)
    
    keys = rp(group_child + 0x710)
    k_cnt = ru32(group_child + 0x71C)
    vals = rp(group_child + 0x6F8)
    
    amt_str = ""
    if keys and 0 < k_cnt < 20 and vals:
        for k in range(k_cnt):
            kname = read_pdx_string(keys + k * 48 + 16)
            if kname == "amount":
                val_elem = rp(vals + k * 8)
                if val_elem:
                    amt_str = read_pdx_string(val_elem + 0x168)
                break
    print(f"Group {i}: '{gname}' = '{amt_str}'")
