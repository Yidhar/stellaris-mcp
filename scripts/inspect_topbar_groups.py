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

def r32(addr):
    d = read(addr, 4)
    return struct.unpack('<I', d)[0] if d else 0

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
ui_win = r64(topbar + 0x78)

c_arr = r64(ui_win + 0x878)
c_cnt = r32(ui_win + 0x884)
print(f'TopBar ui_win has {c_cnt} child containers:')
for j in range(c_cnt):
    child = r64(c_arr + j * 8)
    # Check name in child: where is CUIElement name?
    # In CUIElement, name is CPdxString at +0x18 or +0x20
    name = extract_str(child + 0x18)
    if not name:
        name = extract_str(child + 0x20)
    if not name:
        name = extract_str(child + 0x30)
    
    # Check amount textbox inside child
    keys = r64(child + 0x710)
    k_cnt = r32(child + 0x71C)
    vals = r64(child + 0x6F8)
    amount_txt = ''
    if keys and vals and 0 < k_cnt < 20:
        for k in range(k_cnt):
            k_name = extract_str(keys + k * 48 + 16)
            val = r64(vals + k * 8)
            txt = extract_str(val + 0x168)
            if k_name == 'amount':
                amount_txt = txt
    print(f'  [{j:2d}] child 0x{child:X} name="{name}" amount="{amount_txt}"')

kernel32.CloseHandle(h)
