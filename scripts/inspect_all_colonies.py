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

# Let's inspect other colonies in colony_vec:
cmgr = rp(base + 0x3112F50)
carr = rp(cmgr + 0x18)
player = rp(carr + 8)

colony_vec = rp(player + 0x2F68)
colony_cnt = ctypes.c_uint32()
kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(player + 0x2F74), ctypes.byref(colony_cnt), 4, None)

colony_mgr = rp(base + 0x3113148)
colony_arr = rp(colony_mgr + 0x18)
colony_cap = ctypes.c_uint32()
kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(colony_mgr + 0x20), ctypes.byref(colony_cap), 4, None)

print(f"Total colonies: {colony_cnt.value}")
for i in range(colony_cnt.value):
    cid = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(colony_vec + i * 4), ctypes.byref(cid), 4, None)
    slot = cid.value & 0xFFFFFF
    c_obj = rp(colony_arr + slot * 16 + 8)
    if not c_obj: continue
    s450 = read_pdx_string(c_obj + 0x450)
    pops = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(c_obj + 0x4A4), ctypes.byref(pops), 4, None)
    
    # Check planet class at +0x1B8 or similar
    # Let's check other strings
    strings = []
    for off in range(0, 0x500, 8):
        s = read_pdx_string(c_obj + off)
        if s and 2 < len(s) < 40 and not s.startswith(" "):
            strings.append(f"+0x{off:X}:'{s}'")
    print(f"Colony {i} (id={cid.value}, pops={pops.value}): {', '.join(strings[:5])}")

