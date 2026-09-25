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

def read_u32(addr):
    b = read_bytes(addr, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 else 0

mgr = read_u64(base + 0x3113140)
table = read_u64(mgr + 0x18)
col0 = read_u64(table + 8) # slot 0: table + 0 * 16 + 8
print(f"Colony mgr: 0x{mgr:X}, table: 0x{table:X}")
print(f"Colony 0 ptr: 0x{col0:X}")
for off in range(0, 0x40, 4):
    val = read_u32(col0 + off)
    print(f"  col0 + 0x{off:02X}: 0x{val:08X} ({val})")

# Also check planet 3 (Earth)
pmgr = read_u64(base + 0x3113128)
ptable = read_u64(pmgr + 0x18)
p3 = read_u64(ptable + 3 * 16 + 8)
print(f"\nEarth (Planet 3) ptr: 0x{p3:X}")
print(f"  Earth + 0xE0: 0x{read_u32(p3 + 0xE0):X} (colony_id)")
print(f"  Earth + 0xE4: 0x{read_u32(p3 + 0xE4):X} (queue_id)")

# Deposit 309
dmgr = read_u64(base + 0x3112FB0)
dtable = read_u64(dmgr + 0x18)
d309 = read_u64(dtable + 309 * 16 + 8)
print(f"\nDeposit 309 ptr: 0x{d309:X}")
for off in range(0, 0x30, 4):
    val = read_u32(d309 + off)
    print(f"  d309 + 0x{off:02X}: 0x{val:08X} ({val})")

