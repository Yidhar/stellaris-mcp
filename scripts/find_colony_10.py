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

colony_mgr = read_u64(base + 0x3110CF8)
cap = read_u32(colony_mgr + 0x20)
arr = read_u64(colony_mgr + 0x18)

for i in range(cap):
    obj = read_u64(arr + i * 16 + 8)
    if obj:
        b = read_u32(obj + 0x1280) & 0xFF
        if b == 10:
            # what is this colony?
            planet_id = read_u32(obj + 0x18)
            print(f"Colony index {i}: obj=0x{obj:X}, planet_id={planet_id}, +0x30={read_u32(obj+0x30)}")
