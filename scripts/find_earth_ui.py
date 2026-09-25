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

idler = rp(base + 0x3113180)
print('Idler:', hex(idler))
for off in range(0x500, 0x1500, 8):
    view = rp(idler + off)
    if view > 0x10000 and view < 0x7FFFFFFFFFFF:
        s = read_pdx_string(view + 0x68)
        if s:
            if any(k in s.lower() for k in ['outliner', 'planet', 'colony', 'status']):
                print(f'Idler + 0x{off:X} (view=0x{view:X}) name="{s}"')
