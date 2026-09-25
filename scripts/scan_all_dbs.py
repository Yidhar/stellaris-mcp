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

for off in range(0x3112E00, 0x3113250, 8):
    ptr = rp64(base + off)
    if ptr and 0x20000000000 <= ptr <= 0x24000000000:
        arr = rp64(ptr + 0x18)
        cap = rp32(ptr + 0x20)
        # Check if arr looks valid and cap >= 100
        if 100 <= cap <= 100000 and 0x20000000000 <= arr <= 0x24000000000:
            # Check slot 1557 if cap > 1557
            item_1557 = rp64(arr + 1557 * 16 + 8) if cap > 1557 else 0
            # Also check slot 0 and 11
            item_0 = rp64(arr + 0 * 16 + 8)
            item_11 = rp64(arr + 11 * 16 + 8)
            vt_0 = rp64(item_0) - base if item_0 else 0
            print(f'0x{off:x}: cap={cap}, item1557={hex(item_1557)}, item_0 vt={hex(vt_0)}, item_11={hex(item_11)}')
