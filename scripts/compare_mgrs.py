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

# Compare 0x3112F70 vs 0x3113148
for off in [0x3112F70, 0x3112F78, 0x3113148, 0x3110CF8]:
    mgr = read_u64(base + off)
    cap = read_u32(mgr + 0x20) if mgr else 0
    arr = read_u64(mgr + 0x18) if mgr else 0
    earth_obj = read_u64(arr + 11 * 16 + 8) if arr and cap > 11 else 0
    vt = read_u64(earth_obj) - base if earth_obj else 0
    print(f"Off 0x{off:X}: mgr=0x{mgr:X}, cap={cap}, slot 11 obj=0x{earth_obj:X}, vt=0x{vt:X}")
    if earth_obj:
        # Check what strings or fields it has
        buf = ctypes.create_string_buffer(64)
        read = ctypes.c_size_t()
        # let's read some bytes around +0x18, +0x28, etc.
        print(f"   +0x18: 0x{read_u32(earth_obj + 0x18):X}, +0x20: 0x{read_u32(earth_obj + 0x20):X}, +0x28: 0x{read_u32(earth_obj + 0x28):X}")
