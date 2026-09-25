import sys
sys.path.append(r'D:\stellarismcp\scripts')
import ctypes, reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
h_proc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def rp(a):
    v = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 8, None)
    return v.value

def ru32(a):
    v = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 4, None)
    return v.value

def read_pdx_string(addr):
    cap = rp(addr + 24)
    sz = rp(addr + 16)
    if sz == 0 or sz > 512: return ''
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = rp(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

def get_colony_construction(colony_obj):
    if not colony_obj: return None
    f_f78 = rp(colony_obj + 0xf78)
    slot = f_f78 & 0xFFFFFFFF
    
    mgr_3113128 = rp(base + 0x3113128)
    if not mgr_3113128: return None
    arr_3113128 = rp(mgr_3113128 + 0x18)
    cap_3113128 = ru32(mgr_3113128 + 0x20)
    if slot >= cap_3113128: return None
    slot_obj = rp(arr_3113128 + slot * 16 + 8)
    if not slot_obj: return None

    c4 = ru32(slot_obj + 0x20 + 0xc4)
    mgr_eb8 = rp(base + 0x3112EB8)
    if not mgr_eb8: return None
    arr_eb8 = rp(mgr_eb8 + 0x18)
    cap_eb8 = ru32(mgr_eb8 + 0x20)
    slot_eb8 = c4 & 0xFFFFFF
    if slot_eb8 >= cap_eb8: return None
    queue_obj = rp(arr_eb8 + slot_eb8 * 16 + 8)
    if not queue_obj: return None

    q_cnt = ru32(queue_obj + 0x2c)
    if q_cnt == 0: return None
    q_items = rp(queue_obj + 0x20)
    if not q_items: return None
    item_id = ru32(q_items)

    mgr_ea8 = rp(base + 0x3112EA8)
    if not mgr_ea8: return None
    arr_ea8 = rp(mgr_ea8 + 0x18)
    cap_ea8 = ru32(mgr_ea8 + 0x20)
    slot_ea8 = item_id & 0xFFFFFF
    if slot_ea8 >= cap_ea8: return None
    item_obj = rp(arr_ea8 + slot_ea8 * 16 + 8)
    if not item_obj: return None

    prog = ru32(item_obj + 0x28)
    tot = ru32(item_obj + 0x30)
    action_obj = rp(item_obj + 0x18)
    key = ""
    if action_obj:
        def_obj = rp(action_obj + 8)
        if def_obj:
            key = read_pdx_string(def_obj + 0x20)

    pct = round(float(prog) / float(tot), 2) if tot > 0 else 0.0
    rem_days = int((tot - prog) / 100000) if tot > prog else 0

    return {
        "key": key,
        "progress": pct,
        "remaining_days": rem_days,
        "prog_raw": prog,
        "tot_raw": tot
    }

cmgr = rp(base + 0x3113140)
arr = rp(cmgr + 0x18)
s0 = rp(arr + 8)
print("Earth construction:", get_colony_construction(s0))

s28 = rp(arr + 28 * 16 + 8)
print("Khor-I construction:", get_colony_construction(s28))
