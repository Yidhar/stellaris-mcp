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

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for rva, name in [(0xAC3B90, "Blocker[0] 0xAC3B90"), (0xAC3C40, "Blocker[3] 0xAC3C40"), (0x1AF620, "Bldg[0] 0x1AF620"), (0xAC4A60, "Bldg[1] 0xAC4A60")]:
    target = base + rva
    code = read_bytes(target, 0x30)
    print(f"\n=== {name} ===")
    for i in cs.disasm(code, target):
        print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")

