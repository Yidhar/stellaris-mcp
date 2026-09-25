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

# Find string "clear_blocker_button" in .rdata
needle = b"clear_blocker_button\0"
print("Searching for 'clear_blocker_button'...")
str_addr = None
for offset in range(0x1800000, 0x2A00000, 0x100000):
    d = read_bytes(base + offset, 0x100000 + len(needle))
    idx = d.find(needle)
    if idx != -1:
        str_addr = base + offset + idx
        print(f"Found string at 0x{str_addr:X} (RVA 0x{str_addr-base:X})")
        break

# Find references to string in .text
if str_addr:
    for offset in range(0x1000, 0x1800000, 0x100000):
        d = read_bytes(base + offset, 0x100000 + 8)
        for i in range(len(d) - 7):
            if d[i] in (0x48, 0x4c) and d[i+1] == 0x8d:
                disp = struct.unpack('<i', d[i+3:i+7])[0]
                target = base + offset + i + 7 + disp
                if target == str_addr:
                    ref_rva = offset + i
                    print(f"Found ref at RVA 0x{ref_rva:X}")

