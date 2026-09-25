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

dummy_queue = 0x1D6C0ED0010
p890 = read_u64(dummy_queue + 0x890)
print(f"dummy_queue + 0x890: 0x{p890:X}")
if p890:
    p100 = read_u64(p890 + 0x100)
    print(f"p890 + 0x100: 0x{p100:X}")

# What about Earth object in 0x3112F70 (r14 = 0x1D865F40B10)?
# Does 0x1D865F40B10 have +0x890?
p890_earth = read_u64(0x1D865F40B10 + 0x890)
print(f"r14 (0x1D865F40B10) + 0x890: 0x{p890_earth:X}")
