import ctypes, struct
from ctypes import wintypes

kernel32 = ctypes.WinDLL('kernel32')
kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.ReadProcessMemory.restype = wintypes.BOOL
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE

h = kernel32.OpenProcess(0x10, False, 71656)

def read(addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t(0)
    if kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(n)):
        return buf.raw[:n.value]
    return None

def r64(addr):
    d = read(addr, 8)
    return struct.unpack('<Q', d)[0] if d else 0

def extract_str(addr):
    raw = read(addr, 32)
    if not raw: return ''
    sz = struct.unpack('<Q', raw[16:24])[0]
    cap = struct.unpack('<Q', raw[24:32])[0]
    if sz == 0 or sz > 256: return ''
    if cap < 16:
        s = raw[:min(sz, 15)]
    else:
        ptr = struct.unpack('<Q', raw[:8])[0]
        s = read(ptr, sz)
    if not s: return ''
    return s.decode('utf-8', errors='ignore').strip('\x00')

base = 0x7FF75ED50000
c_mgr = r64(base + 0x3287788)
c_arr = r64(c_mgr + 0x18)
country = r64(c_arr + 8)

for off in [0x18C0, 0x18D8, 0x18F0, 0x1938, 0x1C80, 0x27E8, 0x2810, 0x2888, 0x2940, 0x2988, 0x2DB8, 0x2E00, 0x2E28, 0x2E80]:
    arr = r64(country + off)
    item = r64(arr)
    vt = r64(item) - base if item else 0
    s1 = extract_str(item + 0x20) if item else ''
    s2 = extract_str(item + 0x30) if item else ''
    s3 = extract_str(item + 0x50) if item else ''
    print(f'Country + 0x{off:04X}: arr=0x{arr:X}, item=0x{item:X}, vt=RVA 0x{vt:X}, s1="{s1}", s2="{s2}", s3="{s3}"')

kernel32.CloseHandle(h)
