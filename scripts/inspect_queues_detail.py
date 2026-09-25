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

for slot, obj_addr in [(25, 0x1D86952FE68), (395, 0x1D8695394B8)]:
    vt = read_u64(obj_addr)
    print(f"\nQueue slot {slot} (0x{obj_addr:X}): vt=0x{vt - base:X}")
    print(f"  +8 (id): {read_u32(obj_addr + 8)}")
    print(f"  +0x18: 0x{read_u32(obj_addr + 0x18):X}")
    print(f"  +0x98 (planet_id): {read_u32(obj_addr + 0x98)}")
    # Check queue items vector (e.g., at +0xa8 or +0xb0)
    for off in range(0, 0x100, 8):
        v = read_u64(obj_addr + off)
        # check if pointer
        if 0x1D000000000 <= v <= 0x1E000000000:
            print(f"  off +0x{off:X}: 0x{v:X}")
