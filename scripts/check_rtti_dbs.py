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

def check_rtti(obj_ptr):
    if not obj_ptr: return "NULL"
    vt = read_u64(obj_ptr)
    if not vt: return "NO_VT"
    col = read_u64(vt - 8)
    if not col: return "NO_COL"
    # MSVC RTTI Complete Object Locator
    # In x64: col + 0x0C is type_desc offset (rva)
    type_desc_rva = read_u32(col + 0x0C)
    if type_desc_rva:
        name_bytes = read_bytes(base + type_desc_rva + 0x10, 64)
        name = name_bytes.split(b'\x00')[0].decode('ascii', errors='ignore')
        return name
    return "UNKNOWN"

for rva, name in [(0x3112FB0, "db1 (0x3112FB0)"), (0x3113140, "db2 (0x3113140)"), (0x3112F50, "db3 (0x3112F50)")]:
    mgr = read_u64(base + rva)
    table = read_u64(mgr + 0x18)
    cap = read_u32(mgr + 0x20)
    print(f"\n--- {name} (mgr RTTI: {check_rtti(mgr)}) ---")
    for i in range(min(cap, 20)):
        item = read_u64(table + i * 16 + 8)
        if item:
            print(f"  Slot {i}: obj=0x{item:X}, RTTI: {check_rtti(item)}")

