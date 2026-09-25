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

def read_u64(addr):
    b = read_bytes(addr, 8)
    return struct.unpack('<Q', b)[0] if len(b) == 8 else 0

mgr = read_u64(base + 0x3113140)
table = read_u64(mgr + 0x18)
col0 = read_u64(table + 8)

val_f78 = read_u64(col0 + 0xF78)
print(f"Colony 0 at 0x{col0:X}")
print(f"col0 + 0xF78: 0x{val_f78:X}")

# What function is called at 0xAC4873?
# In 0xAC47B0:
# 0xAC4873: call 0x7ff77889c0c0 -> RVA 0xCCC0C0
# Let's check disasm of 0xCCC0C0!

cs_target = base + 0xCCC0C0
import capstone
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
code = read_bytes(cs_target, 0x50)
print("\nDisasm of 0xCCC0C0 (called with rcx = [col0 + 0xF78]):")
for i in cs.disasm(code, cs_target):
    print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")

