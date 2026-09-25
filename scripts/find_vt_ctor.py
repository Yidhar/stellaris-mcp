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

# Find where vtable 0x239B930 is assigned (i.e. constructor)
vt_addr = base + 0x239B930
vt_bytes = struct.pack('<Q', vt_addr)

print(f"Searching references to 0x{vt_addr:X}...")
for offset in range(0x1000, 0x1800000, 0x100000):
    d = read_bytes(base + offset, 0x100000 + 8)
    pos = 0
    while True:
        idx = d.find(vt_bytes, pos)
        if idx == -1: break
        print(f"  Found absolute ref at RVA 0x{offset + idx:X}")
        pos = idx + 1

