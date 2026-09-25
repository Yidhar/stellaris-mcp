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

print(f"Base: 0x{base:X}")
print("Checking vtable +0x48 and +0x60 with 0x2390F28:")
vt = base + 0x2390F28
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, size, ctypes.byref(read))
    return buf.raw[:read.value]

def read_u64(addr):
    b = read_bytes(addr, 8)
    return struct.unpack('<Q', b)[0] if len(b) == 8 else 0

fn_cost = read_u64(vt + 0x48)
fn_canbuild = read_u64(vt + 0x60)
print(f"vt + 0x48 (CalcCost): 0x{fn_cost:X} (RVA 0x{fn_cost - base:X})")
print(f"vt + 0x60 (CanBuild): 0x{fn_canbuild:X} (RVA 0x{fn_canbuild - base:X})")

