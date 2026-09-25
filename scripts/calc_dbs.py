import ctypes
import win32process
import win32api
import capstone
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

# Calculate RVA of databases in 0xAC47B0:
# 0xAC47C5: lea/mov rdx, [rip + 0x264e7e4] -> next rip = 0xAC47CC
db1 = 0xAC47CC + 0x264E7E4
# 0xAC482F: mov rdx, [rip + 0x264e90a] -> next rip = 0xAC4836
db2 = 0xAC4836 + 0x264E90A
# 0xAC488A: mov r8, [rip + 0x264e6bf] -> next rip = 0xAC4891
db3 = 0xAC4891 + 0x264E6BF

print(f"db1: RVA 0x{db1:X}")
print(f"db2: RVA 0x{db2:X}")
print(f"db3: RVA 0x{db3:X}")

# Search callers of 0x800500 and 0x800450
for target, name in [(0x800500, "0x800500"), (0x800450, "0x800450")]:
    print(f"\nCallers of {name}:")
    for offset in range(0x1000, 0x1800000, 0x100000):
        data = read_bytes(base + offset, 0x100000 + 8)
        for i in range(len(data) - 5):
            if data[i] == 0xE8:
                rel = struct.unpack('<i', data[i+1:i+5])[0]
                target_rva = offset + i + 5 + rel
                if target_rva == target:
                    print(f"  Call at RVA 0x{offset + i:X}")

