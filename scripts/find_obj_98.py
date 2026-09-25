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

# Let's check all objects in 0x3112F78 (Starbase mgr), 0x3110CF8 (Colony mgr), 0x3112F70 (Planet mgr), 0x3112EB8 (Queue mgr)
for off in [0x3112F78, 0x3110CF8, 0x3112F70, 0x3112EB8]:
    mgr = read_u64(base + off)
    if not mgr: continue
    cap = read_u32(mgr + 0x20)
    arr = read_u64(mgr + 0x18)
    if not arr or cap == 0: continue
    found = []
    for i in range(cap):
        obj = read_u64(arr + i * 16 + 8)
        if obj:
            # check +0x98
            v98 = read_u32(obj + 0x98)
            if v98 == 11:
                found.append((i, obj, read_u32(obj + 0x880), read_u32(obj + 8)))
    print(f"Off 0x{off:X} (cap {cap}): found {len(found)} objects with +0x98 == 11:")
    for f in found:
        print(f"  slot {f[0]}: obj=0x{f[1]:X}, +0x880=0x{f[2]:X}, +8=0x{f[3]:X}")
