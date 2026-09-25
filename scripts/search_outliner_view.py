import sys, ctypes
sys.path.append(r'D:\stellarismcp\scripts')
import reload_dll, inject

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

idler = rp(base + 0x3113180)

# Check each view at idler + off for children controls
for off in range(0xB00, 0xC50, 8):
    view = rp(idler + off)
    # Check all pointers inside view (up to 0x200)
    found_earth = False
    for v_off in range(0, 0x200, 8):
        sub = rp(view + v_off)
        if sub > 0x10000 and sub < 0x7FFFFFFFFFFF:
            # check strings in sub
            for s_off in range(0, 0x1000, 8):
                s = read_pdx_string(sub + s_off)
                if '地球' in s or '科尔' in s:
                    print(f"MATCH at idler+0x{off:X} (view=0x{view:X}) -> sub+0x{s_off:X}: '{s}'")
                    found_earth = True
                    break
        if found_earth: break
