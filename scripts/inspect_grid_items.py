import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def read_ptr(addr):
    val = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 8, None)
    return val.value

def read_u32(addr):
    val = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 4, None)
    return val.value

def read_pdx_string(addr):
    cap = read_ptr(addr + 24)
    sz = read_ptr(addr + 16)
    if sz == 0 or sz > 512: return ""
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = read_ptr(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

grid_entry = 0x2B4A0659AD0
grid = read_ptr(grid_entry)
print(f"grid: 0x{grid:X}")
print(f"VT of grid: 0x{read_ptr(grid):X}")

# Check children of grid
c_arr = read_ptr(grid + 0x878)
c_cnt = read_u32(grid + 0x884)
print(f"grid children count: {c_cnt}")

# Also check grid items at +0x6F8 or +0x710 or other offsets
for off in range(0, 0x900, 8):
    val = read_ptr(grid + off)
    # Check if val is a vector
    if val > 0x10000000000 and val < 0x7FFFFFFFFFFF:
        # maybe an array of pointers
        p0 = read_ptr(val)
        if p0 > 0x10000000000 and p0 < 0x7FFFFFFFFFFF:
            p0_vt = read_ptr(p0)
            if p0_vt > base and p0_vt < base + 0x3000000:
                print(f"Potential object array at +0x{off:X} (0x{val:X}): first elem 0x{p0:X} (VT 0x{p0_vt:X})")
