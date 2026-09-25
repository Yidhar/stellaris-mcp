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

pmgr = read_u64(base + 0x3113128)
ptable = read_u64(pmgr + 0x18)
p3 = read_u64(ptable + 3 * 16 + 8) # Planet 3 (Earth)

print(f"Earth: 0x{p3:X}")
print("Examining Earth fields:")
for off in [0x18, 0xC0, 0xC4, 0xE0, 0xE4, 0xF70, 0xF78]:
    val32 = read_u32(p3 + off)
    val64 = read_u64(p3 + off)
    print(f"  Earth + 0x{off:03X}: u32=0x{val32:X} ({val32}), u64=0x{val64:X}")

# What was plVar6 in CPlanetViewDepositEntry::OnClearBlocker?
# plVar6 = CColonyCarrier::LookupCarrierRef(...)
# On Colony 0:
cmgr = read_u64(base + 0x3113140)
ctable = read_u64(cmgr + 0x18)
col0 = read_u64(ctable + 8)
print(f"\nColony 0: 0x{col0:X}")
for off in [0x10, 0x18, 0xC0, 0xC4, 0xF70, 0xF78]:
    val32 = read_u32(col0 + off)
    val64 = read_u64(col0 + off)
    print(f"  Colony 0 + 0x{off:03X}: u32=0x{val32:X} ({val32}), u64=0x{val64:X}")

