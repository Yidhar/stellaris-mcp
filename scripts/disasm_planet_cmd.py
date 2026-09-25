import ctypes
from ctypes import wintypes
import capstone

# OpenProcess
PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

# Find stellaris.exe PID
import win32process
import win32api
import win32gui

# Let's find process ID for stellaris.exe
import subprocess
out = subprocess.check_output("tasklist /FI \"IMAGENAME eq stellaris.exe\" /FO CSV", shell=True).decode('gbk', errors='ignore')
lines = [l.strip().split('","') for l in out.strip().splitlines() if "stellaris.exe" in l]
if not lines:
    print("stellaris.exe not found")
    exit(1)

pid = int(lines[0][1].strip('"'))
print(f"Stellaris PID: {pid}")

# Get module base address
import win32process
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
modules = win32process.EnumProcessModules(h_process)
base = modules[0]
print(f"Base address: 0x{base:X}")

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    bytesRead = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, size, ctypes.byref(bytesRead))
    return buf.raw[:bytesRead.value]

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

target_rva = 0x11DF700
size = 0x250

code = read_bytes(base + target_rva, size)
for i in cs.disasm(code, base + target_rva):
    print(f"0x{i.address - base:X}: {i.mnemonic} {i.op_str}")
