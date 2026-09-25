import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def rp(a):
    v = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 8, None)
    return v.value

def ru32(a):
    v = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 4, None)
    return v.value

def get_rtti(vt):
    col = rp(vt - 8)
    if not col: return ''
    b_off = ru32(col + 20)
    td_rva = ru32(col + 12)
    img_base = vt - b_off
    td = img_base + td_rva
    name_buf = (ctypes.c_char * 64)()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(td + 16), name_buf, 64, None)
    return bytes(name_buf).split(b'\x00')[0].decode('latin1', errors='ignore')

for off in range(0x3112F00, 0x3113100, 8):
    mgr = rp(base + off)
    if mgr > 0x10000 and mgr < 0x7FFFFFFFFFFF:
        vt = rp(mgr)
        rtti = get_rtti(vt)
        arr = rp(mgr + 0x18)
        cap = ru32(mgr + 0x20)
        print(f"base + 0x{off:X}: mgr=0x{mgr:X}, vt=0x{vt:X} ('{rtti}'), cap={cap}")
