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

phys_vec_ptr = rp(player + 0x3AB8 + 8)
phys_vec_cnt = ru32(player + 0x3AB8 + 0x14)

fmgr = rp(base + 0x3113008)
f_arr = rp(fmgr + 0x18)
f_cap = ru32(fmgr + 0x20)

for i in range(phys_vec_cnt):
    fid = ru32(phys_vec_ptr + i * 4)
    slot = fid & 0xFFFFFF
    flt = rp(f_arr + slot * 16 + 8)
    if not flt: continue
    
    # Check all strings in flt from 0 to 0x200
    strings = []
    for off in range(0, 0x200, 8):
        s = read_pdx_string(flt + off)
        if s and len(s) > 1 and not s.startswith(" "):
            strings.append(f"+0x{off:X}:'{s}'")
    print(f"Fleet {fid}: {', '.join(strings)}")
