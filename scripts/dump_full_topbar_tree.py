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

def dump_node(c, depth=0):
    indent = '  ' * depth
    # find name of c: search in first 0x100 bytes for CPdxString
    node_name = ''
    for o in [0x18, 0x20, 0x28, 0x30, 0x38, 0x58, 0x60]:
        s = extract_str(c + o)
        if s and len(s) > 2 and not s.startswith('W') and not s.startswith('0'):
            node_name = s
            break
            
    # controls
    keys = r64(c + 0x710)
    k_cnt = r32(c + 0x71C)
    vals = r64(c + 0x6F8)
    ctrls = []
    if keys and vals and 0 < k_cnt < 50:
        for k in range(k_cnt):
            kn = extract_str(keys + k * 48 + 16)
            val = r64(vals + k * 8)
            txt = extract_str(val + 0x168)
            ctrls.append(f'{kn}="{txt}"')
            
    print(f'{indent}Container 0x{c:X} [{node_name}] ctrls: {ctrls}')
    
    # recurse
    c_arr = r64(c + 0x878)
    c_cnt = r32(c + 0x884)
    if c_arr and 0 < c_cnt < 50:
        for j in range(c_cnt):
            ch = r64(c_arr + j * 8)
            if ch:
                dump_node(ch, depth + 1)

base = 0x7FF75ED50000
idler = r64(base + 0x3287900)
topbar = r64(idler + 0xAC8)
ui_win = r64(topbar + 0x78)

dump_node(ui_win)

kernel32.CloseHandle(h)
