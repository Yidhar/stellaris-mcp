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

colony_vec = rp(player + 0x2F68)
colony_cnt = ru32(player + 0x2F74)

pmgr = rp(base + 0x3112FA8)
p_arr = rp(pmgr + 0x18)
p_cap = ru32(pmgr + 0x20)

print(f"Colonies count: {colony_cnt}, pmgr cap: {p_cap}")

for i in range(colony_cnt):
    cid = ru32(colony_vec + i * 4)
    slot = cid & 0xFFFFFF
    p_obj = rp(p_arr + slot * 16 + 8)
    name = ""
    # Look for name in p_obj
    if p_obj:
        for off in [0x20, 0xA8, 0xF8, 0x168]:
            s = read_pdx_string(p_obj + off)
            if s and len(s) > 1:
                name = s
                break
    print(f"Colony {i}: id={cid}, p_obj=0x{p_obj:X}, name='{name}'")
