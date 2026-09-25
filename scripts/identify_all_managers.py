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

def read_pdx_string(addr):
    cap = rp(addr + 24)
    sz = rp(addr + 16)
    if sz == 0 or sz > 512: return ""
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = rp(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

for off in range(0x3112F00, 0x3113160, 8):
    mgr = rp(base + off)
    if not (mgr > 0x10000 and mgr < 0x7FFFFFFFFFFF): continue
    cap = ru32(mgr + 0x20)
    arr = rp(mgr + 0x18)
    if not (arr > 0x10000 and arr < 0x7FFFFFFFFFFF and cap > 0): continue
    
    # Check first valid element
    first_obj = None
    first_id = 0
    for s in range(min(cap, 50)):
        p = rp(arr + s * 16 + 8)
        if p and p > 0x10000 and p < 0x7FFFFFFFFFFF:
            first_obj = p
            first_id = ru32(p + 8)
            break
    if not first_obj: continue
    
    # Collect some strings from first_obj
    strings = []
    for o in range(0, 0x180, 8):
        s = read_pdx_string(first_obj + o)
        if s and 2 < len(s) < 40 and any(c.isalpha() for c in s):
            strings.append(f"+0x{o:X}:'{s}'")
    
    print(f"Manager base+0x{off:X}: cap={cap}, first_id={first_id}, obj=0x{first_obj:X}")
    if strings:
        print(f"   Strings: {', '.join(strings[:4])}")

