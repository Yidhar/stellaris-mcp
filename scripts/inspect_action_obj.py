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

# Queue 2: items
db_eb8 = rp64(base + 0x3112EB8)
arr_eb8 = rp64(db_eb8 + 0x18)
q2 = rp64(arr_eb8 + 2 * 16 + 8)

db_ea8 = rp64(base + 0x3112EA8)
arr_ea8 = rp64(db_ea8 + 0x18)

arr_items = rp64(q2 + 0x20)
for i in range(6):
    item_id = rp32(arr_items + i * 4)
    i_obj = rp64(arr_ea8 + (item_id & 0xFFFFFF) * 16 + 8)
    act = rp64(i_obj + 0x18)
    vt = rp64(act) - base if act else 0
    print(f'Item {i} act={hex(act)} vt={hex(vt)}:')
    for off in range(0, 0x28, 4):
        print(f'  +0x{off:02x}: {rp32(act + off)} (0x{rp32(act + off):x})')
