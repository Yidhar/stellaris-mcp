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

def r64(addr):
    raw = read_bytes(addr, 8)
    return struct.unpack('<Q', raw)[0] if len(raw) == 8 else 0

def read_std_string(addr):
    cap = r64(addr + 0x18)
    sz = r64(addr + 0x10)
    if sz == 0 or sz > 500: return ''
    if cap < 16:
        buf = ctypes.create_string_buffer(sz)
        read = ctypes.c_size_t()
        kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, sz, ctypes.byref(read))
        return buf.raw[:read.value].decode('utf-8', errors='replace')
    else:
        ptr = r64(addr)
        if ptr < 0x10000 or ptr > 0x7FFFFFFFFFFF: return ''
        buf = ctypes.create_string_buffer(sz)
        read = ctypes.c_size_t()
        kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(ptr), buf, sz, ctypes.byref(read))
        return buf.raw[:read.value].decode('utf-8', errors='replace')

p = 0x0000026F19F382B0
raw = read_bytes(p, 0x120)
for off in range(0, len(raw), 8):
    q = struct.unpack('<Q', raw[off:off+8])[0]
    s = read_std_string(p + off)
    s_str = f"-> str: '{s}'" if s else ""
    print(f"+0x{off:02X}: 0x{q:016X}  {s_str}")
