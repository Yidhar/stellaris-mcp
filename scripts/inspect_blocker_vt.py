import ctypes
import win32process
import win32api
import capstone
import psutil
import struct

# Find stellaris.exe PID
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

vt_rva = 0x2390F30
vt_addr = base + vt_rva
print(f"Target vtable: 0x{vt_addr:X} (RVA 0x{vt_rva:X})")

# Let's inspect the vtable entries at 0x2390F30
print("Vtable entries:")
vt_raw = read_bytes(vt_addr, 30 * 8)
for i in range(30):
    fn = struct.unpack('<Q', vt_raw[i*8:(i+1)*8])[0]
    print(f"  [{i:2d}] 0x{fn:X} (RVA 0x{fn - base:X})")

# Also search for caller of 0xA35290
call_bytes = struct.pack('<i', 0xA35290) # Relative calls might be call rel32
print("\nSearching for callers of 0xA35290...")
for offset in range(0x1000, 0x1800000, 0x100000):
    data = read_bytes(base + offset, 0x100000 + 8)
    for i in range(len(data) - 5):
        if data[i] == 0xE8: # call rel32
            rel = struct.unpack('<i', data[i+1:i+5])[0]
            target_rva = offset + i + 5 + rel
            if target_rva == 0xA35290:
                print(f"Found call to 0xA35290 at RVA 0x{offset + i:X}")

