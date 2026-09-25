import ctypes
import win32process
import win32api
import subprocess

out = subprocess.check_output("tasklist /FI \"IMAGENAME eq stellaris.exe\" /FO CSV", shell=True).decode('gbk', errors='ignore')
lines = [l.strip().split('","') for l in out.strip().splitlines() if "stellaris.exe" in l]
pid = int(lines[0][1].strip('"'))
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_u64(addr):
    buf = ctypes.c_uint64()
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), ctypes.byref(buf), 8, ctypes.byref(read))
    return buf.value

# 0xCB801A: mov rax, qword ptr [rip + 0x2459fcf] -> next rip = base + 0xCB8021
dummy_queue_addr = base + 0xCB8021 + 0x2459fcf
print(f"dummy_queue_addr: 0x{dummy_queue_addr - base:X}")
dummy_queue = read_u64(dummy_queue_addr)
print(f"dummy_queue: 0x{dummy_queue:X}")
if dummy_queue:
    vt = read_u64(dummy_queue)
    print(f"dummy_queue vtable: 0x{vt - base:X}")
