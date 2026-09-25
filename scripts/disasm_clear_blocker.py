import ctypes
import win32process
import win32api
import win32gui
import capstone
import psutil

# Find stellaris.exe PID
pid = None
for p in psutil.process_iter(['pid', 'name']):
    if p.info['name'] and p.info['name'].lower() == 'stellaris.exe':
        pid = p.info['pid']
        break

if not pid:
    print("stellaris.exe not running!")
    exit(1)

print(f"stellaris.exe PID: {pid}")
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]
print(f"Base: 0x{base:X}")

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, size, ctypes.byref(read))
    return buf.raw[:read.value]

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

print("\n=== Disasm around crash at 0xA3530A (0xA35280 - 0xA35350) ===")
code = read_bytes(base + 0xA35280, 0x100)
for i in cs.disasm(code, base + 0xA35280):
    print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")

