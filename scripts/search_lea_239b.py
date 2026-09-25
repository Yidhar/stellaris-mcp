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

print("Searching for lea to 0x239B000 - 0x239C000...")
for offset in range(0x1000, 0x1800000, 0x100000):
    d = read_bytes(base + offset, 0x100000 + 8)
    for i in range(len(d) - 7):
        if d[i] in (0x48, 0x4c) and d[i+1] == 0x8d: # lea reg64, [rip + disp32]
            disp = struct.unpack('<i', d[i+3:i+7])[0]
            target_rva = offset + i + 7 + disp
            if 0x239B000 <= target_rva <= 0x239C000:
                print(f"  RVA 0x{offset + i:X}: lea to 0x{target_rva:X}")

