import sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.append('scripts')
import ctypes, struct
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(read)):
        return buf.raw[:read.value]
    return b''

def read_std_string(addr):
    raw = read_bytes(addr, 32)
    if len(raw) < 32: return ""
    cap = struct.unpack('<Q', raw[0x18:0x20])[0]
    sz = struct.unpack('<Q', raw[0x10:0x18])[0]
    if sz == 0 or sz > 500: return ""
    if cap < 16:
        return raw[:sz].decode('utf-8', errors='replace')
    else:
        ptr = struct.unpack('<Q', raw[:8])[0]
        if ptr < 0x10000 or ptr > 0x7FFFFFFFFFFF: return ""
        buf = read_bytes(ptr, sz)
        return buf.decode('utf-8', errors='replace')

p = 0x2700216CE80 # Corvette CShipSize
raw = read_bytes(p, 0x300)
for off in range(0, len(raw), 8):
    q = struct.unpack('<Q', raw[off:off+8])[0]
    u1, u2 = struct.unpack('<II', raw[off:off+8])
    val_str = ""
    if 0x10000 < q < 0x7FFFFFFFFFFF:
        s = read_std_string(q)
        if s: val_str = f"-> str: '{s}'"
    print(f"+0x{off:03X}: 0x{q:016X} ({u1:5d}, {u2:5d}) {val_str}")
