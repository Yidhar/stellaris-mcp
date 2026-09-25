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

# Vtable of CBuildableClearDepositBlocker: 0x2390F30
# Vtable of CBuildableBuilding: 0x2391298
raw_blocker = read_bytes(base + 0x2390F30, 30 * 8)
raw_bldg = read_bytes(base + 0x2391298, 30 * 8)

print("Index | Blocker RVA | Bldg RVA")
print("------------------------------")
for i in range(25):
    fn_bl = struct.unpack('<Q', raw_blocker[i*8:(i+1)*8])[0] - base
    fn_bd = struct.unpack('<Q', raw_bldg[i*8:(i+1)*8])[0] - base
    print(f"[{i:2d}]  | 0x{fn_bl:07X} | 0x{fn_bd:07X}")

