import sys, os, ctypes
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

cmgr = read_ptr(base + 0x3112F50)
carr = read_ptr(cmgr + 0x18)
player = read_ptr(carr + 8)

t_arr = read_ptr(player + 0x16E0 + 0x20)
t_cnt = read_u32(player + 0x16E0 + 0x28)

print(f"Researched techs count: {t_cnt}")
tech_keys = []
for i in range(t_cnt):
    p = read_ptr(t_arr + i * 8)
    if not p: continue
    k = read_pdx_string(p + 0x20)
    tech_keys.append(k)

for target in [
    "tech_mine_volatile_motes",
    "tech_mine_exotic_gases",
    "tech_mine_rare_crystals",
    "tech_mine_living_metal",
    "tech_mine_zro",
    "tech_mine_dark_matter"
]:
    print(f"Has {target}: {target in tech_keys}")
