import sys, ctypes
sys.path.append(r'D:\stellarismcp\scripts')
import reload_dll, inject

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

# 1. Player established colonies
cmgr = rp(base + 0x3112F50)
carr = rp(cmgr + 0x18)
player = rp(carr + 8)

est_vec = rp(player + 0x2F80)
est_cnt = ru32(player + 0x2F8C)
established_ids = [ru32(est_vec + i * 4) for i in range(est_cnt)]
print(f"Established colonies ({est_cnt}): {established_ids}")

# 2. Galaxy sectors
game = rp(base + 0x3112A08)
sec_vec = rp(game + 0x7B8)
sec_cnt = ru32(game + 0x7C4)

sec_mgr = rp(base + 0x3112FA8)
sec_arr = rp(sec_mgr + 0x18)
sec_cap = ru32(sec_mgr + 0x20)

colony_mgr = rp(base + 0x3113148)
colony_arr = rp(colony_mgr + 0x18)

core_sector = None
assigned_colonies = set()

for i in range(sec_cnt):
    sid = ru32(sec_vec + i * 4)
    slot = sid & 0xFFFFFF
    if slot >= sec_cap: continue
    sec_obj = rp(sec_arr + slot * 16 + 8)
    if not sec_obj: continue
    
    ptr = rp(sec_obj + 0x140)
    sz = ru32(sec_obj + 0x14C)
    if not ptr or sz == 0: continue
    
    planets_in_sec = [ru32(ptr + k * 4) for k in range(sz)]
    # Check if Earth (11) is in planets_in_sec
    if 11 in planets_in_sec:
        # This is the Core Sector!
        core_colonies = [p for p in planets_in_sec if p in established_ids]
        core_sector = {
            "sector_id": 0,
            "sector_name": "核心星域 (Core Sector)",
            "is_core": True,
            "colonies": core_colonies
        }
        assigned_colonies.update(core_colonies)

frontier_colonies = [p for p in established_ids if p not in assigned_colonies]
frontier_sector = {
    "sector_id": 1,
    "sector_name": "边境星区 (Frontier Sector)",
    "is_core": False,
    "colonies": frontier_colonies
}

print("\n--- Core Sector ---")
print(core_sector)
for cid in core_sector["colonies"]:
    c_obj = rp(colony_arr + (cid & 0xFFFFFF) * 16 + 8)
    sys_name = read_pdx_string(c_obj + 0x450)
    pops = ru32(c_obj + 0x4A4)
    print(f"  Colony {cid}: sys='{sys_name}', pops={pops}")

print("\n--- Frontier Sector ---")
print(frontier_sector)
for cid in frontier_sector["colonies"]:
    c_obj = rp(colony_arr + (cid & 0xFFFFFF) * 16 + 8)
    sys_name = read_pdx_string(c_obj + 0x450)
    pops = ru32(c_obj + 0x4A4)
    print(f"  Colony {cid}: sys='{sys_name}', pops={pops}")
