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

def read_u64(addr):
    b = read_bytes(addr, 8)
    return struct.unpack('<Q', b)[0] if len(b) == 8 else 0

vt = base + 0x2390F28
print(f"Primary vtable at 0x{vt:X} (RVA 0x2390F28):")
for i in range(16):
    fn = read_u64(vt + i * 8)
    print(f"  [{i:2d}] (offset +0x{i*8:02X}): 0x{fn:X} (RVA 0x{fn - base:X})")

# Let's disasm entry [0] at 0x2390F28:
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
code = read_bytes(read_u64(vt), 0x30)
print(f"\nDisasm of [0] ({read_u64(vt)-base:X}):")
for i in cs.disasm(code, read_u64(vt)):
    print(f"  0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")

