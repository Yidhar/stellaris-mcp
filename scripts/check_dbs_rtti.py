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

def read_u64(addr):
    b = read_bytes(addr, 8)
    return struct.unpack('<Q', b)[0] if len(b) == 8 else 0

def read_u32(addr):
    b = read_bytes(addr, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 else 0

for rva, name in [(0x3112FB0, "0x3112FB0 (db1)"), (0x3113140, "0x3113140 (db2)"), (0x3112F50, "0x3112F50 (db3)"), (0x3113128, "0x3113128 (planets)"), (0x3113148, "0x3113148 (solar systems)")]:
    ptr = read_u64(base + rva)
    if ptr:
        vt = read_u64(ptr)
        col_vt = read_u64(vt - 8) if vt else 0 # RTTI Complete Object Locator
        # Let's also check table capacity and count
        table = read_u64(ptr + 0x18)
        cap = read_u32(ptr + 0x20)
        cnt = read_u32(ptr + 0x24)
        print(f"{name}: ptr=0x{ptr:X}, vtable=0x{vt:X} (RVA 0x{vt-base:X}), cap={cap}, cnt={cnt}")
    else:
        print(f"{name}: ptr=NULL")

