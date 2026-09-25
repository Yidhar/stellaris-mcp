import ctypes
import sys
sys.path.append(r'D:\stellarismcp\scripts')
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = ctypes.windll.kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

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

db_p = rp64(base + 0x3113128)
arr_p = rp64(db_p + 0x18)
cap_p = rp32(db_p + 0x20)

def find_planet(p_id):
    # 1. Direct match by Planet ID IF it has an active colony/colonization
    slot = p_id & 0xFFFFFF
    if slot < cap_p:
        p = rp64(arr_p + slot * 16 + 8)
        if p and rp32(p + 0x18) == p_id:
            col_id = rp32(p + 0xe0)
            if col_id != 0xFFFFFFFF or rp32(p + 8) != 0:
                return p
    # 2. Match by System ID (+0x50) where colony is present
    for s in range(cap_p):
        p = rp64(arr_p + s * 16 + 8)
        if not p: continue
        sys_id = rp32(p + 0x50)
        if sys_id == p_id:
            col_id = rp32(p + 0xe0)
            if col_id != 0xFFFFFFFF or rp32(p + 8) != 0:
                return p
    # 3. Fallback direct match even if uncolonized
    if slot < cap_p:
        p = rp64(arr_p + slot * 16 + 8)
        if p and rp32(p + 0x18) == p_id:
            return p
    return 0

for test_id in [11, 3, 63, 752]:
    p = find_planet(test_id)
    if p:
        pid = rp32(p + 0x18)
        sys_id = rp32(p + 0x50)
        cid = rp32(p + 0xe0)
        qid = rp32(p + 0xe4)
        name = read_pdx_string(p + 0x108)
        print(f'Query {test_id:3d} -> Planet {pid}: sys={sys_id}, cid={cid}, qid={qid}, name="{name}"')
    else:
        print(f'Query {test_id:3d} -> NOT FOUND')
