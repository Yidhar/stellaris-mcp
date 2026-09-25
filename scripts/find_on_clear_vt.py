import ctypes
import win32process
import win32api
import psutil
import struct
import capstone

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

# Search around 0x11FF000 to 0x1205000 for lea pointing to 0x2390F30 (blocker vtable)
blocker_vt = base + 0x2390F30
print(f"Searching for lea to blocker vtable (0x{blocker_vt:X}) near 0x1200000...")
d = read_bytes(base + 0x11F0000, 0x20000)
for i in range(len(d) - 7):
    if d[i] in (0x48, 0x4c) and d[i+1] == 0x8d:
        disp = struct.unpack('<i', d[i+3:i+7])[0]
        target = base + 0x11F0000 + i + 7 + disp
        if target == blocker_vt:
            print(f"FOUND ONCLEARBLOCKER! RVA 0x{0x11F0000 + i:X}")

