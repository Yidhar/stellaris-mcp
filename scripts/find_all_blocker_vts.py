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

# Search for 0xAC4340 (CalcCost for blocker) in all vtables (.rdata / .data)
target = base + 0xAC4340
target_bytes = struct.pack('<Q', target)

print(f"Searching references to Blocker CalcCost (0x{target:X})...")
for offset in range(0x1800000, 0x2A00000, 0x100000):
    d = read_bytes(base + offset, 0x100000 + 8)
    pos = 0
    while True:
        idx = d.find(target_bytes, pos)
        if idx == -1: break
        rva = offset + idx
        print(f"  Found ref at RVA 0x{rva:X}")
        pos = idx + 1

