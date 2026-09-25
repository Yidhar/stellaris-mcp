import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def rp(a):
    v = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 8, None)
    return v.value

def ru32(a):
    v = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 4, None)
    return v.value

def ri64(a):
    v = ctypes.c_int64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 8, None)
    return v.value

cmgr = rp(base + 0x3112F50)
carr = rp(cmgr + 0x18)
player = rp(carr + 8)

target_val = -136.81
target_raw = int(target_val * 100000)

print(f"Searching for raw value around {target_raw} (-136.81)...")

# Search in CCountry memory (say from 0 to 0x4000)
found_in_country = []
for off in range(0, 0x4000, 8):
    val = ri64(player + off)
    val_float = val / 100000.0
    if abs(val_float - target_val) < 0.1:
        print(f"Match in CCountry + 0x{off:X}: raw={val}, float={val_float:.4f}")
        found_in_country.append(off)

# Also check pointers inside CCountry (e.g. bal_ptr at +0x2B40, or other balance managers)
bal_ptr = rp(player + 0x2B40)
print(f"Checking bal_ptr (+0x2B40): 0x{bal_ptr:X}")
for off in range(0, 0x200, 8):
    p = rp(bal_ptr + off)
    val = ri64(bal_ptr + off)
    val_float = val / 100000.0
    if abs(val_float - target_val) < 0.1:
        print(f"Match in bal_ptr + 0x{off:X}: raw={val}, float={val_float:.4f}")
    # If p is a heap array, check elements
    if p > 0x10000 and p < 0x7FFFFFFFFFFF:
        # Check first 30 elements of p
        for idx in range(30):
            elem_val = ri64(p + idx * 8)
            elem_float = elem_val / 100000.0
            if abs(elem_float - target_val) < 0.1:
                print(f"Match in bal_ptr + 0x{off:X} -> array[{idx}]: raw={elem_val}, float={elem_float:.4f}")

