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

# Queue DB: 0x3112EB8
db_eb8 = rp64(base + 0x3112EB8)
arr_eb8 = rp64(db_eb8 + 0x18)
cap_eb8 = rp32(db_eb8 + 0x20)

# QueueItem DB: 0x3112EA8
db_ea8 = rp64(base + 0x3112EA8)
arr_ea8 = rp64(db_ea8 + 0x18)
cap_ea8 = rp32(db_ea8 + 0x20)

for q_id in [2, 29, 552, 553, 554]:
    slot = q_id & 0xFFFFFF
    q_obj = rp64(arr_eb8 + slot * 16 + 8)
    if not q_obj:
        print(f'Queue {q_id}: NULL')
        continue
    
    # Let us inspect q_obj
    token = rp32(q_obj + 8)
    owner_country = rp32(q_obj + 0x44)
    # CPdxArray items: offset 0x18 or 0x20?
    arr_20 = rp64(q_obj + 0x20)
    cnt_28 = rp32(q_obj + 0x28)
    cnt_2c = rp32(q_obj + 0x2c)
    val_30 = rp32(q_obj + 0x30)
    
    print(f'=== Queue {q_id} (token={token}, ptr={hex(q_obj)}) ===')
    print(f'  owner_country={owner_country}, cnt_28={cnt_28}, cnt_2c={cnt_2c}, val_30={val_30}')
    
    # Read items
    items = []
    if arr_20 and cnt_2c > 0 and cnt_2c < 100:
        for i in range(cnt_2c):
            item_id = rp32(arr_20 + i * 4)
            i_slot = item_id & 0xFFFFFF
            i_obj = rp64(arr_ea8 + i_slot * 16 + 8)
            key = ''
            prog = 0
            tot = 0
            if i_obj:
                prog = rp32(i_obj + 0x28)
                tot = rp32(i_obj + 0x30)
                act = rp64(i_obj + 0x18)
                if act:
                    bldg_def = rp64(act + 8)
                    if bldg_def:
                        key = read_pdx_string(bldg_def + 0x20)
            items.append((item_id, key, prog, tot))
    print(f'  Items ({len(items)}): {items}')
