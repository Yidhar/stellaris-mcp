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

c_mgr = rp64(base + 0x3113140)
arr = rp64(c_mgr + 0x18)
colony_0 = rp64(arr + 8) # slot 0 = Earth colony
print(f'Colony 0 at {hex(colony_0)}')

# Scan first 0x1500 bytes of colony_0 for 46
matches = []
for off in range(0, 0x1500, 4):
    v32 = rp32(colony_0 + off)
    if v32 == 46:
        matches.append(hex(off))

print('Offsets containing 46 (Queue 46):', matches)
