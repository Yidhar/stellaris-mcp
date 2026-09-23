import ctypes
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
hProc = reload_dll.kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_u64(addr):
    buf = ctypes.c_uint64()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 8, None)
    return buf.value

def read_u32(addr):
    buf = ctypes.c_uint32()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 4, None)
    return buf.value

def read_pdx_string(addr):
    buf = (ctypes.c_char * 32)()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 32, None)
    raw_size = int.from_bytes(buf[16:24], 'little')
    raw_cap = int.from_bytes(buf[24:32], 'little')
    if raw_size == 0 or raw_size > 500:
        return ""
    if raw_cap < 16:
        return bytes(buf[:raw_size]).decode('utf-8', errors='ignore')
    else:
        ptr = int.from_bytes(buf[0:8], 'little')
        if ptr:
            sbuf = (ctypes.c_char * raw_size)()
            reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(ptr), ctypes.byref(sbuf), raw_size, None)
            return bytes(sbuf).decode('utf-8', errors='ignore')
    return ""

# Design Manager at base + 0x3112980
p_mgr = read_u64(base + 0x3112980)
arr = read_u64(p_mgr + 0x18)
cap = read_u32(p_mgr + 0x20)
print(f"Total designs in manager: {cap}")

did = 1606
candidate = read_u64(arr + (did & 0xFFFFFF) * 16 + 8)
print(f"Design 1606 candidate: 0x{candidate:X}")

name = read_pdx_string(candidate + 0x48)
print(f"Name: {name}")

p_sub = read_u64(candidate + 0x20)
print(f"p_sub: 0x{p_sub:X}")

# Inspect all pointers in p_sub
for off in range(0, 0x100, 8):
    val = read_u64(p_sub + off)
    cnt = read_u32(p_sub + off + 8)
    # see if val looks like an array
    if val > 0x10000 and val < 0x7FFFFFFFFFFF:
        # try reading first element
        elem0 = read_u64(val)
        print(f"p_sub + 0x{off:02X}: ptr=0x{val:X}, next_u32={cnt}, elem0=0x{elem0:X}")

# Now inspect comp_arr at p_sub + 0x30
comp_arr = read_u64(p_sub + 0x30)
comp_cnt = read_u32(p_sub + 0x38)
print(f"\nComp array at 0x30: count = {comp_cnt}")
for c in range(comp_cnt):
    p_comp = read_u64(comp_arr + c * 8)
    key = read_pdx_string(p_comp + 0x1B0)
    print(f"  Core [{c}]: {key} (0x{p_comp:X})")

reload_dll.kernel32.CloseHandle(hProc)
