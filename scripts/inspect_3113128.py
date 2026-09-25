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

def read_pdx_string(addr):
    size = rp64(addr + 0x10)
    if size < 16:
        buf = (ctypes.c_char * 16)()
        ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, 16, None)
        return bytes(buf).split(b'\x00')[0].decode('utf-8', errors='ignore')
    else:
        ptr = rp64(addr)
        if not ptr: return ''
        buf = (ctypes.c_char * min(size + 1, 128))()
        ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, len(buf), None)
        return bytes(buf).split(b'\x00')[0].decode('utf-8', errors='ignore')

# 0x3113128
db = rp64(base + 0x3113128)
arr = rp64(db + 0x18)

for idx in [0, 11, 1557]:
    obj = rp64(arr + idx * 16 + 8)
    vt = rp64(obj) - base if obj else 0
    print(f'=== DB 0x3113128 slot {idx} at {hex(obj)} (vt={hex(vt)}) ===')
    if obj:
        token = rp32(obj + 8)
        print(f'  token: {token}')
        # Check all strings
        found_strs = []
        for off in range(0, 0x300, 8):
            s = read_pdx_string(obj + off)
            if len(s) > 1 and all(32 <= ord(c) < 127 for c in s):
                found_strs.append((hex(off), s))
        print(f'  strs: {found_strs}')
        # Check fields
        for off in range(0, 0x100, 4):
            v = rp32(obj + off)
            if v in [2032, 2, 46, 29, 554, 56]:
                print(f'  +0x{off:02x}: {v} (matches known queue!)')
