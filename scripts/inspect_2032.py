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

# Queue 2032
db_eb8 = rp64(base + 0x3112EB8)
arr_eb8 = rp64(db_eb8 + 0x18)
q_2032 = rp64(arr_eb8 + 2032 * 16 + 8)

db_ea8 = rp64(base + 0x3112EA8)
arr_ea8 = rp64(db_ea8 + 0x18)

arr_items = rp64(q_2032 + 0x20)
item_id = rp32(arr_items)
i_obj = rp64(arr_ea8 + (item_id & 0xFFFFFF) * 16 + 8)
act = rp64(i_obj + 0x18)
bldg_def = rp64(act + 8)
key = read_pdx_string(bldg_def + 0x20)
print(f'Queue 2032 is building: {key}')

# Now check 1557 in Planet DB and Colony DB
db_planet = rp64(base + 0x3112F78)
arr_planet = rp64(db_planet + 0x18)
p_1557 = rp64(arr_planet + (1557 & 0xFFFFFF) * 16 + 8)

db_colony = rp64(base + 0x3113140)
arr_colony = rp64(db_colony + 0x18)
c_1557 = rp64(arr_colony + (1557 & 0xFFFFFF) * 16 + 8)

print(f'Carrier 1557 in Planet DB: {hex(p_1557)}')
print(f'Carrier 1557 in Colony DB: {hex(c_1557)}')

if p_1557:
    print('Planet 1557 fields:')
    print(f'  +0x880 (queue): {rp32(p_1557 + 0x880)}')
    print(f'  +0x0c4 (queue): {rp32(p_1557 + 0x0c4)}')
    # Check if 2032 is anywhere in p_1557
    for off in range(0, 0x1000, 4):
        if rp32(p_1557 + off) == 2032:
            print(f'  Planet 1557 + 0x{off:03x} == 2032!')

if c_1557:
    print('Colony 1557 fields:')
    for off in range(0, 0x1500, 4):
        if rp32(c_1557 + off) == 2032:
            print(f'  Colony 1557 + 0x{off:03x} == 2032!')
