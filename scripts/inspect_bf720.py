import ctypes, struct
from ctypes import wintypes

kernel32 = ctypes.WinDLL('kernel32')
kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.ReadProcessMemory.restype = wintypes.BOOL
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE

h = kernel32.OpenProcess(0x10, False, 73956)

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

def r8(addr):
    d = read(addr, 1)
    return struct.unpack('<B', d)[0] if d else 0

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

c = 0x20B218BF720
print(f"Container 0x{c:X}:")
# Let's inspect its controls at 0x6F8/0x710 and children at 0x878
keys_arr = r64(c + 0x710)
cnt = r32(c + 0x71C)
vals_arr = r64(c + 0x6F8)
print(f"Controls count: {cnt}")
if keys_arr and vals_arr and 0 < cnt < 100:
    for i in range(cnt):
        k_name = extract_str(keys_arr + i * 48 + 16)
        val = r64(vals_arr + i * 8)
        txt = extract_str(val + 0x168) if val else ''
        print(f'  Control: "{k_name}" -> val=0x{val:X}, txt="{txt}"')

c_arr = r64(c + 0x878)
c_cnt = r32(c + 0x884)
print(f"Children count: {c_cnt}")
if c_arr and 0 < c_cnt < 100:
    for j in range(c_cnt):
        child = r64(c_arr + j * 8)
        c_name = extract_str(child + 0x18)
        print(f"  Child[{j}]: 0x{child:X} ('{c_name}')")
