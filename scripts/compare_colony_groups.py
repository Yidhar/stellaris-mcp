import sys, ctypes
sys.path.append(r'D:\stellarismcp\scripts')
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
h_proc = kernel32.OpenProcess(0x1F0FFF, False, pid)

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
    if sz == 0 or sz > 512: return ''
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = rp(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

colony_mgr = rp(base + 0x3113148)
colony_arr = rp(colony_mgr + 0x18)

group_established = [11, 12, 31, 54, 63, 76, 144, 149, 161]
group_other = [82, 87, 118, 173]

print("=== Group 1 (in +0x2F80) ===")
for cid in group_established:
    slot = cid & 0xFFFFFF
    c_obj = rp(colony_arr + slot * 16 + 8)
    sys_name = read_pdx_string(c_obj + 0x450)
    pops = ru32(c_obj + 0x4A4)
    print(f"Colony {cid}: sys='{sys_name}', pops={pops}")

print("\n=== Group 2 (NOT in +0x2F80) ===")
for cid in group_other:
    slot = cid & 0xFFFFFF
    c_obj = rp(colony_arr + slot * 16 + 8)
    sys_name = read_pdx_string(c_obj + 0x450)
    pops = ru32(c_obj + 0x4A4)
    print(f"Colony {cid}: sys='{sys_name}', pops={pops}")
