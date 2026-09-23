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

comp_db = read_u64(base + 0x3156198)
arr = read_u64(comp_db + 0x20)
cnt = read_u64(comp_db + 0x28) & 0xFFFFFFFF

test_keys = ["SMALL_DARK_MATTER_DEFLECTOR", "LARGE_DARK_MATTER_DEFLECTOR", "LARGE_ARMOR_5", "CORVETTE_DARK_MATTER_REACTOR"]
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
    c_type = read_u32(tmpl + 0x10) # component type
    print(f"\n[{k}] @ 0x{tmpl:X}, type={c_type}, Power={pwr}")
    
    # Check CStaticModifier at +0x2b0
    # Let's inspect +0x2b0
    # CStaticModifier structure:
    # let's dump +0x2b0 to +0x350
    mod_addr = tmpl + 0x2b0
    # In CModifier / CStaticModifier:
    # let's see what pointers are at mod_addr
    for off in range(0, 0x60, 8):
        v = read_u64(mod_addr + off)
        vi = read_i64(mod_addr + off)
        print(f"  mod+{hex(off)}: 0x{v:X} (i64={vi}, /100k={vi/100000.0:.2f})")

reload_dll.kernel32.CloseHandle(hProc)
