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

# Earth: slot 3 in 0x3113128
db_planet = rp64(base + 0x3113128)
arr_planet = rp64(db_planet + 0x18)
earth = rp64(arr_planet + 3 * 16 + 8)

# Colony 0: slot 0 in 0x3113140
db_colony = rp64(base + 0x3113140)
arr_colony = rp64(db_colony + 0x18)
colony_0 = rp64(arr_colony + 0 * 16 + 8)

print(f'Earth: {hex(earth)}, Colony 0: {hex(colony_0)}')

# Check references from Earth to Colony 0
for off in range(0, 0x500, 4):
    v32 = rp32(earth + off)
    v64 = rp64(earth + off)
    if v64 == colony_0:
        print(f'Earth + 0x{off:03x} points to Colony 0 directly!')
    if v32 == 0 and off in [0xc0, 0xc4, 0xe0, 0x4b0, 0x4b8]:
        print(f'Earth + 0x{off:03x} == 0 (colony id 0)')

# Check references from Colony 0 to Earth
for off in range(0, 0x1200, 4):
    v32 = rp32(colony_0 + off)
    v64 = rp64(colony_0 + off)
    if v64 == earth:
        print(f'Colony 0 + 0x{off:03x} points to Earth directly!')
    if v32 == 3:
        print(f'Colony 0 + 0x{off:03x} == 3 (planet id 3)')
