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

# Find function start before 0x120124E
d = read_bytes(base + 0x1200E00, 0x450)
# Look backwards from 0x44E
pos = 0x44E
while pos > 0:
    if d[pos-1] == 0xCC and d[pos] != 0xCC:
        fn_start = 0x1200E00 + pos
        print(f"Function before 0x1201250 starts at 0x{fn_start:X} (RVA 0x{fn_start:X})")
        break
    pos -= 1

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
code = read_bytes(base + fn_start, 0x150)
for i in cs.disasm(code, base + fn_start):
    print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")

