import ctypes
import win32process
import win32api
import subprocess
import capstone

out = subprocess.check_output("tasklist /FI \"IMAGENAME eq stellaris.exe\" /FO CSV", shell=True).decode('gbk', errors='ignore')
lines = [l.strip().split('","') for l in out.strip().splitlines() if "stellaris.exe" in l]
pid = int(lines[0][1].strip('"'))
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, size, ctypes.byref(read))
    return buf.raw[:read.value]

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Disassemble backward from 0x11F7261 to find function prologue
code = read_bytes(base + 0x11F7000, 0x300)
print("=== 0x11F7000 ... 0x11F7300 ===")
for i in cs.disasm(code, base + 0x11F7000):
    if i.mnemonic in ['push', 'sub', 'call', 'ret'] or i.address - base in [0x11F7257, 0x11F725A, 0x11F7261]:
        print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")

# Disassemble backward from 0x14CCF9E
code = read_bytes(base + 0x14CCE00, 0x250)
print("\n=== 0x14CCE00 ... 0x14CD000 ===")
for i in cs.disasm(code, base + 0x14CCE00):
    if i.mnemonic in ['push', 'sub', 'call', 'ret'] or i.address - base in [0x14CCF91, 0x14CCF94, 0x14CCF9E]:
        print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")
