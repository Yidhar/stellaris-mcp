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

earth_obj = 0x1D865F40B10
q25_ptr = 0x1D86952FE68
q395_ptr = 0x1D8695394B8

print("Scanning earth_obj (0x1D865F40B10) for queue pointers/IDs...")
for off in range(0, 0x1500, 4):
    v32 = read_u32(earth_obj + off)
    v64 = read_u64(earth_obj + off) if off % 8 == 0 else 0
    if v64 in [q25_ptr, q395_ptr]:
        print(f"Found queue ptr 0x{v64:X} at earth_obj + 0x{off:X}!")
    if v32 in [25, 395]:
        print(f"Found queue ID {v32} at earth_obj + 0x{off:X} (u32)")

# Also scan Earth Colony object (0x1D863CA19D8)
colony_obj = 0x1D863CA19D8
print("\nScanning colony_obj (0x1D863CA19D8) for queue pointers/IDs...")
for off in range(0, 0x1500, 4):
    v32 = read_u32(colony_obj + off)
    v64 = read_u64(colony_obj + off) if off % 8 == 0 else 0
    if v64 in [q25_ptr, q395_ptr]:
        print(f"Found queue ptr 0x{v64:X} at colony_obj + 0x{off:X}!")
    if v32 in [25, 395]:
        print(f"Found queue ID {v32} at colony_obj + 0x{off:X} (u32)")
