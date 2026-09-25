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

cmgr = rp(base + 0x3112F50)
carr = rp(cmgr + 0x18)
player = rp(carr + 8)

colony_vec = rp(player + 0x2F68)
colony_cnt = ru32(player + 0x2F74)

for i in range(colony_cnt):
    cid = ru32(colony_vec + i * 4)
    slot = cid & 0xFFFFFF
    c_obj = rp(colony_arr + slot * 16 + 8)
    sys_name = read_pdx_string(c_obj + 0x450)
    pops = ru32(c_obj + 0x4A4)
    
    # Check offsets between 0x400 and 0x600:
    # Is there a colonization progress float or flag?
    # What is at +0x4A0, +0x4A8, +0x4AC, etc.?
    ints = [ru32(c_obj + 0x480 + k*4) for k in range(16)]
    print(f"[{i:2d}] id={cid:3d} pops={pops:2d} sys={sys_name:18s} @0x480={ints}")
