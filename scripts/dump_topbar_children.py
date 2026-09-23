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
idler = r64(base + 0x3287900)
topbar = r64(idler + 0xAC8)
print(f'TopBar: 0x{topbar:X}')

topbar_data = read(topbar, 0x1000)
for off in range(0, len(topbar_data) - 16, 8):
    p = struct.unpack('<Q', topbar_data[off:off+8])[0]
    cnt = struct.unpack('<I', topbar_data[off+8:off+12])[0]
    cap = struct.unpack('<I', topbar_data[off+12:off+16])[0]
    if 5 <= cnt <= 200 and cnt == cap and 0x10000 < p < 0x7FFFFFFFFFFF:
        print(f'Child vector @ +0x{off:03X}: cnt={cnt}')
        for j in range(cnt):
            child = r64(p + j * 8)
            name = extract_str(child + 0x18) if child else ''
            txt1 = extract_str(child + 0x168) if child else ''
            print(f'  [{j:2d}] child 0x{child:X} name="{name}" txt="{txt1}"')

kernel32.CloseHandle(h)
