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

p_mgr = read_u64(base + 0x3112980)
arr = read_u64(p_mgr + 0x18)
did = 1606
candidate = read_u64(arr + (did & 0xFFFFFF) * 16 + 8)
p_sub = read_u64(candidate + 0x20)

sec_arr = read_u64(p_sub + 0x18)
sec_cnt = read_u32(p_sub + 0x20)

print(f"Sections count: {sec_cnt}")
for s in range(sec_cnt):
    p_sec = read_u64(sec_arr + s * 8)
    name = read_pdx_string(p_sec + 0x18)
    print(f"\n--- Section [{s}]: {name} (0x{p_sec:X}) ---")
    for off in range(0, 0x80, 8):
        v = read_u64(p_sec + off)
        print(f"  +0x{off:02X}: 0x{v:X}")
    
    # Also check template at +0x10 or +0x08?
    tmpl = read_u64(p_sec + 0x10)
    if tmpl > 0x10000:
        tmpl_key = read_pdx_string(tmpl + 0x18)
        print(f"  Template (+0x10): {tmpl_key} (0x{tmpl:X})")
        # Template slots?
        t_slots_arr = read_u64(tmpl + 0x68) # or where?
        t_slots_cnt = read_u32(tmpl + 0x70)
        print(f"  Template slots cnt at 0x70: {t_slots_cnt}")

reload_dll.kernel32.CloseHandle(hProc)
