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

item_obj = 0x2B465639120
print(f'item_obj: 0x{item_obj:X}')

# Check pointers in item_obj
for off in range(0, 0x150, 8):
    p = rp(item_obj + off)
    if p > 0x10000 and p < 0x7FFFFFFFFFFF:
        # Check if p is a string or an object with strings
        s = read_pdx_string(p)
        s_68 = read_pdx_string(p + 0x68) if not s else ''
        # Also check if p is a string at p directly (raw c-str)
        c_buf = (ctypes.c_char * 32)()
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(p), c_buf, 32, None)
        c_str = bytes(c_buf).split(b'\x00')[0].decode('utf-8', errors='ignore')
        print(f'  +0x{off:X} -> 0x{p:X}: pdx="{s}" c_str="{c_str}"')
