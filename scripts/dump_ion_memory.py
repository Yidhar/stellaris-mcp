import ctypes
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
hProc = reload_dll.kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_u64(addr):
    buf = ctypes.c_uint64()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 8, None)
    return buf.value

def read_i64(addr):
    buf = ctypes.c_int64()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 8, None)
    return buf.value

def read_u32(addr):
    buf = ctypes.c_uint32()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 4, None)
    return buf.value

def read_float(addr):
    buf = ctypes.c_float()
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

# Find PERDITION_BEAM_ION in component_db
comp_db = read_u64(base + 0x3156198)
arr = read_u64(comp_db + 0x20)
cnt = read_u64(comp_db + 0x28) & 0xFFFFFFFF

ion_tmpl = None
titan_tmpl = None
for i in range(cnt):
    set_ptr = read_u64(arr + i * 8)
    c_vec = read_u64(set_ptr + 0xB8)
    c_cnt = read_u64(set_ptr + 0xC0) & 0xFFFFFFFF
    for j in range(c_cnt):
        tmpl = read_u64(c_vec + j * 8)
        k = read_pdx_string(tmpl + 0x1B0)
        if k == "PERDITION_BEAM_ION":
            ion_tmpl = tmpl
        elif k == "PERDITION_BEAM_TITAN":
            titan_tmpl = tmpl

print(f"PERDITION_BEAM_ION at 0x{ion_tmpl:X}")
print(f"PERDITION_BEAM_TITAN at 0x{titan_tmpl:X}")

def dump_weapon_stats(name, tmpl):
    print(f"\n=== {name} (0x{tmpl:X}) ===")
    # Clausewitz CFixedPoint is 64-bit int divided by 1000 or 100000? Let's check:
    # 5000 damage is 5000 * 1000 = 5000000 or 5000 * 100000?
    min_dmg_raw = read_i64(tmpl + 0x11c8)
    delta_dmg_raw = read_i64(tmpl + 0x11d0)
    print(f"min_dmg_raw: {min_dmg_raw}, delta_dmg_raw: {delta_dmg_raw}")
    
    # Dump offsets 0x1178 to 0x1250 as i64 and float
    for off in range(0x1178, 0x1220, 8):
        val_i64 = read_i64(tmpl + off)
        val_f = read_float(tmpl + off)
        val_u32 = read_u32(tmpl + off)
        print(f"  +{hex(off)}: i64={val_i64} (scaled={val_i64/1000.0:.2f}), f32={val_f:.2f}, u32={val_u32}")

dump_weapon_stats("PERDITION_BEAM_ION", ion_tmpl)
reload_dll.kernel32.CloseHandle(hProc)
