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

entries_container = 0x20B218E4590
print(f"entries_container: 0x{entries_container:X}")

keys_arr = r64(entries_container + 0x710)
cnt = r32(entries_container + 0x71C)
vals_arr = r64(entries_container + 0x6F8)
print(f"Controls count: {cnt}")
for i in range(cnt):
    k_name = extract_str(keys_arr + i * 48 + 16)
    val = r64(vals_arr + i * 8)
    vt = r64(val)
    print(f"  Control '{k_name}': val=0x{val:X}, vt=0x{vt:X}")
    if k_name == "entries":
        # Inspect smoothListBoxType
        listbox = val
        print(f"  Inspecting smoothListBox at 0x{listbox:X}:")
        for off in range(0, 0x120, 8):
            v = r64(listbox + off)
            # check if v looks like vector or count
            print(f"    +0x{off:03X}: 0x{v:016X}")

kernel32.CloseHandle(h)
