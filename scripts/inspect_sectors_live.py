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

game = rp(base + 0x3112A08)
sec_vec = rp(game + 0x7B8)
sec_cnt = ru32(game + 0x7C4)

sec_mgr = rp(base + 0x3112FA8)
sec_arr = rp(sec_mgr + 0x18)

for i in range(sec_cnt):
    sid = ru32(sec_vec + i * 4)
    slot = sid & 0xFFFFFF
    sec_obj = rp(sec_arr + slot * 16 + 8)
    print(f"=== Sector {i} (id={sid}, addr=0x{sec_obj:X}) ===")
    
    strings = []
    for off in range(0, 0x300, 8):
        s = read_pdx_string(sec_obj + off)
        if s and len(s) > 1 and not s.startswith(' '):
            strings.append(f"+0x{off:X}:'{s}'")
    print("  Strings:", strings)
    
    nums = [f"+0x{off:X}:{ru32(sec_obj + off)}" for off in range(0, 0x80, 4)]
    print("  First 16 ints:", nums[:16])
