import sys; sys.path.append('scripts')
import ctypes, inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
hProc = reload_dll.kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_u64(addr):
    buf = ctypes.c_uint64()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 8, None)
    return buf.value

def read_i64(addr):
    buf = ctypes.c_int64()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 8, None)
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

comp_db = read_u64(base + 0x3156198)
arr = read_u64(comp_db + 0x20)
cnt = read_u64(comp_db + 0x28) & 0xFFFFFFFF

test_keys = ["SMALL_PLASMA_3", "MEDIUM_PLASMA_3", "LARGE_PLASMA_3", "KINETIC_ARTILLERY_2"]
found = {}

for i in range(cnt):
    set_ptr = read_u64(arr + i * 8)
    c_vec = read_u64(set_ptr + 0xB8)
    c_cnt = read_u64(set_ptr + 0xC0) & 0xFFFFFFFF
    for j in range(c_cnt):
        tmpl = read_u64(c_vec + j * 8)
        k = read_pdx_string(tmpl + 0x1B0)
        if k in test_keys:
            found[k] = tmpl

for k, tmpl in found.items():
    pwr = read_i64(tmpl + 0x2A8) / 100000.0
    min_d = read_i64(tmpl + 0x1218) / 100000.0
    delta_d = read_i64(tmpl + 0x1220) / 100000.0
    max_d = min_d + delta_d
    rng = read_i64(tmpl + 0x11E8) / 100000.0
    cd = read_i64(tmpl + 0x11E0) / 100000.0
    acc = read_i64(tmpl + 0x11F8) / 100000.0
    trk = read_i64(tmpl + 0x1200) / 100000.0
    hull_m = read_i64(tmpl + 0x1228) / 100000.0
    armor_m = read_i64(tmpl + 0x1230) / 100000.0
    shield_m = read_i64(tmpl + 0x1238) / 100000.0
    print(f"\n[{k}] @ 0x{tmpl:X}:")
    print(f"  Power: {pwr}")
    print(f"  Damage: {min_d:.1f} - {max_d:.1f}")
    print(f"  Range: {rng:.1f}")
    print(f"  Cooldown: {cd:.1f}")
    print(f"  Accuracy: {acc*100:.0f}%")
    print(f"  Tracking: {trk*100:.0f}%")
    print(f"  Hull Mult: {hull_m:.2f} | Armor Mult: {armor_m:.2f} | Shield Mult: {shield_m:.2f}")

reload_dll.kernel32.CloseHandle(hProc)
