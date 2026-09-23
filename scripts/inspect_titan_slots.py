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
    if raw_size == 0 or raw_size > 500: return ''
    if raw_cap < 16: return bytes(buf[:raw_size]).decode('utf-8', errors='ignore')
    ptr = int.from_bytes(buf[0:8], 'little')
    if ptr:
        sbuf = (ctypes.c_char * raw_size)()
        reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(ptr), ctypes.byref(sbuf), raw_size, None)
        return bytes(sbuf).decode('utf-8', errors='ignore')
    return ''

mgr = read_u64(base + 0x3112A08)
arr = read_u64(mgr + 0x20)
cnt = read_u32(mgr + 0x28)
d_1606 = None
for i in range(cnt):
    p = read_u64(arr + i * 8)
    if p and read_u32(p + 0x10) == 1606:
        d_1606 = p
        break

print(f"Titan 1606 at 0x{d_1606:X}")
p_sub = read_u64(d_1606 + 0x20)
sec_arr = read_u64(p_sub + 0x18)
sec_cnt = read_u32(p_sub + 0x20)
for s in range(sec_cnt):
    p_sec = read_u64(sec_arr + s * 8)
    sec_name = read_pdx_string(p_sec + 0x18)
    sec_tmpl = read_u64(p_sec + 0x40)
    tmpl_name = read_pdx_string(sec_tmpl + 0x18)
    print(f"\nSection {s}: '{sec_name}', Template: '{tmpl_name}' (0x{sec_tmpl:X})")
    
    # Template slots
    tmpl_slots_arr = read_u64(sec_tmpl + 0x178)
    tmpl_slots_cnt = read_u32(sec_tmpl + 0x184)
    print(f"  Template slots count: {tmpl_slots_cnt} (arr: 0x{tmpl_slots_arr:X})")
    for k in range(tmpl_slots_cnt):
        slot_addr = tmpl_slots_arr + k * 0x98
        sl_name = read_pdx_string(slot_addr + 0x18)
        print(f"    Tmpl Slot {k}: '{sl_name}' at 0x{slot_addr:X}")
        
    # Installed components in p_sec
    inst_arr = read_u64(p_sec + 0x50)
    inst_cnt = read_u32(p_sec + 0x58)
    print(f"  Installed components count: {inst_cnt} (arr: 0x{inst_arr:X})")
    for k in range(inst_cnt):
        item_addr = inst_arr + k * 0x20
        c_slot_def = read_u64(item_addr + 8)
        c_tmpl = read_u64(item_addr + 0x10)
        c_slot_name = read_pdx_string(c_slot_def + 0x18) if c_slot_def else "none"
        c_comp_key = read_pdx_string(c_tmpl + 0x1B0) if c_tmpl else "none"
        print(f"    Installed {k}: slot='{c_slot_name}' (0x{c_slot_def:X}), comp='{c_comp_key}' (0x{c_tmpl:X})")

reload_dll.kernel32.CloseHandle(hProc)
