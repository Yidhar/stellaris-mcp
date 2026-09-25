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

# In 0x7B9130:
# 0x7B917E: lea rax, [rip + 0x1be00cb] -> next rip = 0x7B9185
str1_addr = base + 0x7B9185 + 0x1BE00CB
print("0x7B9185 string:", read_bytes(str1_addr, 64).split(b'\0')[0].decode('ascii', errors='ignore'))

# In 0xACC830:
# 0xACC979: lea rax, [rip + 0x18c3138] -> next rip = 0xACC980
str2_addr = base + 0xACC980 + 0x18C3138
print("0xACC980 string:", read_bytes(str2_addr, 64).split(b'\0')[0].decode('ascii', errors='ignore'))

