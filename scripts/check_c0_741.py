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

# Player country is at slot 0 of country mgr (0x3112F50)
cmgr = read_u64(base + 0x3112F50)
ctable = read_u64(cmgr + 0x18)
c0 = read_u64(ctable + 8)
print(f"Country 0: 0x{c0:X}")

# In 0xACCAB0:
# mov rcx, rsi (which was Country 0)
# call 0x7ff778311e70 (RVA 0x741E70)
# Let's disasm 0x741E70:
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
target = base + 0x741E70
code = read_bytes(target, 0x30)
print("\nDisasm of 0x741E70:")
for i in cs.disasm(code, target):
    print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")

