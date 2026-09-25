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
print(f"Colony count: {colony_cnt}")

colony_mgr = rp(base + 0x3113148)
colony_arr = rp(colony_mgr + 0x18)
colony_cap = ru32(colony_mgr + 0x20)

for i in range(colony_cnt):
    cid = ru32(colony_vec + i * 4)
    slot = cid & 0xFFFFFF
    c_obj = rp(colony_arr + slot * 16 + 8)
    print(f"Colony {i}: id={cid}, obj=0x{c_obj:X}")
    pops = ru32(c_obj + 0x4A4)
    print(f"   pops={pops}")
    # Inspect strings in c_obj
    for off in range(0, 0x200, 8):
        s = read_pdx_string(c_obj + off)
        if s and len(s) > 1:
            print(f"   +0x{off:X}: '{s}'")
    # Also check sector pointer/id in c_obj
    for off in range(0, 0x500, 4):
        val = ru32(c_obj + off)
        # if val looks interesting
