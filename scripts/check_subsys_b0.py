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

cmgr = read_u64(base + 0x3112F50)
ctable = read_u64(cmgr + 0x18)
c0 = read_u64(ctable + 8)

subsys = read_u64(c0 + 0x2B40)
print(f"Subsystem at c0 + 0x2B40: 0x{subsys:X}")
vt = read_u64(subsys)
print(f"Subsystem vtable: 0x{vt:X} (RVA 0x{vt-base:X})")

# vfunc 0xb0 (entry 22)
fn_b0 = read_u64(vt + 0xB0)
print(f"vfunc 0xB0: 0x{fn_b0:X} (RVA 0x{fn_b0-base:X})")

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
code = read_bytes(fn_b0, 0x50)
print("\nDisasm of vfunc 0xB0:")
for i in cs.disasm(code, fn_b0):
    print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")

