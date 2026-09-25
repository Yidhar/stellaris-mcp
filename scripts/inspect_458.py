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

earth_obj = 0x1D865F40B10
p458 = earth_obj + 0x458
vt458 = read_u64(p458)
print(f"earth_obj + 0x458 vt: 0x{vt458 - base:X}")
fn_10 = read_u64(vt458 + 0x10)
print(f"fn at [vt458 + 0x10]: 0x{fn_10 - base:X}")
