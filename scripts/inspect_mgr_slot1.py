import sys
sys.path.append(r'D:\stellarismcp\scripts')
import ctypes, reload_dll, inject

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

mgr = rp(base + 0x3112EB8)
arr = rp(mgr + 0x18)
cap = ru32(mgr + 0x20)
print(f'Manager cap: {cap}')

slot1_ptr = rp(arr + 1 * 16 + 8)
print(f'Slot 1 obj: 0x{slot1_ptr:X}')

for off in range(0, 0x150, 8):
    s = read_pdx_string(slot1_ptr + off)
    if s:
        print(f'  Slot 1 + 0x{off:X}: "{s}"')

for off in range(0, 0x100, 4):
    v = ru32(slot1_ptr + off)
    if v != 0 and v < 100000:
        print(f'  Slot 1 + 0x{off:X} (u32): {v}')
