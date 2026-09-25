import ctypes, sys
sys.path.append(r'D:\stellarismcp\scripts')
import reload_dll, inject
pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
h_proc = ctypes.windll.kernel32.OpenProcess(0x1F0FFF, False, pid)

def rp64(addr):
    v = ctypes.c_uint64()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(v), 8, None)
    return v.value

def rp32(addr):
    v = ctypes.c_uint32()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(v), 4, None)
    return v.value

def read_pdx_string(addr):
    size = rp64(addr + 0x10)
    if size < 16:
        buf = (ctypes.c_char * 16)()
        ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, 16, None)
        return bytes(buf).split(b'\x00')[0].decode('utf-8', errors='ignore')
    else:
        ptr = rp64(addr)
        if not ptr: return ''
        buf = (ctypes.c_char * min(size + 1, 128))()
        ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, len(buf), None)
        return bytes(buf).split(b'\x00')[0].decode('utf-8', errors='ignore')

# Earth colony (0x3113140, slot 0)
col_db = rp64(base + 0x3113140)
col_arr = rp64(col_db + 0x18)
col0 = rp64(col_arr + 8)

z_db = rp64(base + 0x3113030)
z_arr = rp64(z_db + 0x18)
b_db = rp64(base + 0x3112FF8)
b_arr = rp64(b_db + 0x18)
d_db = rp64(base + 0x3112FF0)
d_arr = rp64(d_db + 0x18)

d_cnt = rp32(col0 + 0x94)
d_ptr = rp64(col0 + 0x88)

print(f"Colony 0: 0x{col0:X}, districts: {d_cnt}")

for i in range(d_cnt):
    did = rp32(d_ptr + i * 4)
    d_obj = rp64(d_arr + (did & 0xFFFFFF) * 16 + 8)
    if not d_obj: continue
    z_cnt = rp32(d_obj + 0x3c)
    z_ptr = rp64(d_obj + 0x30)
    for j in range(z_cnt):
        zid = rp32(z_ptr + j * 4)
        z_obj = rp64(z_arr + (zid & 0xFFFFFF) * 16 + 8)
        if not z_obj: continue
        b_cnt = rp32(z_obj + 0x1b4)
        b_ptr = rp64(z_obj + 0x1a8)
        for k in range(b_cnt):
            bid = rp32(b_ptr + k * 4)
            b_obj = rp64(b_arr + (bid & 0xFFFFFF) * 16 + 8)
            if not b_obj: continue
            b_type = rp64(b_obj + 0x20)
            key = read_pdx_string(b_type + 0x20)
            upg_cnt = rp32(b_type + 0x4a4)
            upg_ptr = rp64(b_type + 0x498)
            print(f"District {did} Zone {zid} (slot {j}) -> bid={bid} (0x{bid:X}): key=\"{key}\", upg_cnt={upg_cnt}")
            if upg_ptr and 0 < upg_cnt < 20:
                for u in range(upg_cnt):
                    target_btype = rp64(upg_ptr + u * 8)
                    t_key = read_pdx_string(target_btype + 0x20)
                    print(f"   -> upgrade {u}: \"{t_key}\" (btype=0x{target_btype:X})")
