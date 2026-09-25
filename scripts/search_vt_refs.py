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

for target, name in [(0x800390, "0x800390"), (0x800500, "0x800500")]:
    target_addr = base + target
    target_bytes = struct.pack('<Q', target_addr)
    print(f"Searching references to {name} (0x{target_addr:X})...")
    # Search data and rdata sections: 0x1800000 to 0x3000000
    for offset in range(0x1800000, 0x2A00000, 0x100000):
        d = read_bytes(base + offset, 0x100000 + 8)
        pos = 0
        while True:
            idx = d.find(target_bytes, pos)
            if idx == -1: break
            print(f"  Found ref in table/vtable at RVA 0x{offset + idx:X}")
            pos = idx + 1

