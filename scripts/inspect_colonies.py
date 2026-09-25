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

colony_obj = 0x1D863CA19D8
print(f"colony_obj + 0x30: 0x{read_u32(colony_obj + 0x30):X}")
print(f"colony_obj vtable: 0x{read_u64(colony_obj) - base:X}")
print(f"colony_obj + 0x18: 0x{read_u32(colony_obj + 0x18):X}")

# What is at colony_obj + 0x1280?
print(f"colony_obj + 0x1280: {read_u32(colony_obj + 0x1280)}")

# Wait, what if [rax + 0x1280] == 0xa? What type of colony is 0xa?
# Let's check all colonies in colony_mgr (0x3110CF8)
colony_mgr = read_u64(base + 0x3110CF8)
cap = read_u32(colony_mgr + 0x20)
arr = read_u64(colony_mgr + 0x18)
print(f"colony_mgr at 0x3110CF8: cap={cap}")
vals = []
for i in range(cap):
    obj = read_u64(arr + i * 16 + 8)
    if obj:
        b1280 = read_u32(obj + 0x1280) & 0xFF
        vals.append(b1280)
print(f"Values at +0x1280 across colonies: {set(vals)}")
