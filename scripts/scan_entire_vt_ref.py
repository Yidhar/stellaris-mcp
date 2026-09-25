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

# Search references to 0x2390F30 anywhere in entire memory (text + rdata + data)
vt_rva = 0x2390F30
target_addr = base + vt_rva
target_bytes = struct.pack('<Q', target_addr)

print(f"Scanning entire PE image for absolute ptr to 0x{target_addr:X}...")
for offset in range(0, 0x3000000, 0x100000):
    d = read_bytes(base + offset, 0x100000 + 8)
    pos = 0
    while True:
        idx = d.find(target_bytes, pos)
        if idx == -1: break
        print(f"  Absolute ptr at RVA 0x{offset + idx:X}")
        pos = idx + 1

print("\nScanning .text for RIP-relative LEA/MOV to 0x2390F30...")
for offset in range(0, 0x1800000, 0x100000):
    d = read_bytes(base + offset, 0x100000 + 8)
    for i in range(len(d) - 4):
        disp = struct.unpack('<i', d[i:i+4])[0]
        # next instruction address = offset + i + 4
        if offset + i + 4 + disp == vt_rva:
            print(f"  RIP-relative ref at RVA 0x{offset + i - 3:X} (disp at +{i})")

