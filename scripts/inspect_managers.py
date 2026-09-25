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

def read_u32(addr):
    buf = ctypes.c_uint32()
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), ctypes.byref(buf), 4, ctypes.byref(read))
    return buf.value

# Let's check what [rip + 0x1f31511] at 0x11DF7E0 is:
# next rip = base + 0x11DF7E7
mgr_7e0 = base + 0x11DF7E7 + 0x1f31511
print(f"mgr_7e0 addr: 0x{mgr_7e0 - base:X}")
print(f"mgr_7e0 val: 0x{read_u64(mgr_7e0):X}")

# What is [rip + 0x1f33703] at 0x11DF846:
# next rip = base + 0x11DF84D
mgr_846 = base + 0x11DF84D + 0x1f33703
print(f"mgr_846 addr: 0x{mgr_846 - base:X}")
print(f"mgr_846 val: 0x{read_u64(mgr_846):X}")

# What is [rip + 0x1f33124] at 0x11DF8DD:
# next rip = base + 0x11DF8E4
mgr_8dd = base + 0x11DF8E4 + 0x1f33124
print(f"mgr_8dd addr: 0x{mgr_8dd - base:X}")
print(f"mgr_8dd val: 0x{read_u64(mgr_8dd):X}")
