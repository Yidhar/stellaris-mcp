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

db_f78 = rp64(base + 0x3112F78)
arr_f78 = rp64(db_f78 + 0x18)
earth = rp64(arr_f78 + 11 * 16 + 8)
print(f'Earth (Planet 11) at {hex(earth)}')

# Let us inspect pointers and arrays in earth
for off in range(0, 0x600, 8):
    val64 = rp64(earth + off)
    val32 = rp32(earth + off)
    if 0x20000000000 <= val64 <= 0x24000000000:
        cnt = rp32(earth + off + 8)
        print(f'  +0x{off:03x}: ptr={hex(val64)}, cnt={cnt}')
        if cnt < 50:
            entries = [rp32(val64 + i * 4) for i in range(min(cnt, 16))]
            print(f'    entries: {entries}')
