import ctypes
import sys
sys.path.append(r'D:\stellarismcp\scripts')
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = ctypes.windll.kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def rp64(addr):
    v = ctypes.c_uint64()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(v), 8, None)
    return v.value

def rp32(addr):
    v = ctypes.c_uint32()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(v), 4, None)
    return v.value

# 1. Planet 11 in DB 0x3112F78
db_f78 = rp64(base + 0x3112F78)
arr_f78 = rp64(db_f78 + 0x18)
planet_11 = rp64(arr_f78 + 11 * 16 + 8)

# 2. Sol in DB 0x3113148
db_148 = rp64(base + 0x3113148)
arr_148 = rp64(db_148 + 0x18)
sol_sys = rp64(arr_148 + 11 * 16 + 8)

# 3. Colony 0 in DB 0x3113140
db_140 = rp64(base + 0x3113140)
arr_140 = rp64(db_140 + 0x18)
colony_0 = rp64(arr_140 + 8)

for name, obj, sz in [('Planet 11', planet_11, 0x1000), ('Sol System', sol_sys, 0x800), ('Colony 0', colony_0, 0x1200)]:
    matches = []
    for off in range(0, sz, 4):
        v = rp32(obj + off)
        if v == 56:
            matches.append(hex(off))
    print(f'{name} matches for 56:', matches)
