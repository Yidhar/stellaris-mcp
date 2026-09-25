import ctypes
import win32process
import win32api
import psutil
import struct

pid = None
for p in psutil.process_iter(['pid', 'name']):
    if p.info['name'] and p.info['name'].lower() == 'stellaris.exe':
        pid = p.info['pid']
        break

h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, size, ctypes.byref(read))
    return buf.raw[:read.value]

def read_u64(addr):
    b = read_bytes(addr, 8)
    return struct.unpack('<Q', b)[0] if len(b) == 8 else 0

def read_u32(addr):
    b = read_bytes(addr, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 else 0

pmgr = read_u64(base + 0x3113128)
ptable = read_u64(pmgr + 0x18)
p3 = read_u64(ptable + 3 * 16 + 8)

print(f"Earth (Planet 3) ptr: 0x{p3:X}")
print(f"p3 + 0x18: 0x{read_u32(p3 + 0x18):X} ({read_u32(p3 + 0x18)})")
print(f"p3 + 0x10: 0x{read_u32(p3 + 0x10):X} ({read_u32(p3 + 0x10)})")

