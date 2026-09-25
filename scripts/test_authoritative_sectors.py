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

# 1. Player country and colony IDs
cmgr = rp(base + 0x3112F50)
carr = rp(cmgr + 0x18)
player = rp(carr + 8)

player_colony_vec = rp(player + 0x2780)
player_colony_cnt = ru32(player + 0x278c)
player_colony_ids = set(ru32(player_colony_vec + i * 4) for i in range(player_colony_cnt))
capital_planet_id = ru32(player + 0x1d04)

print(f"Player Colony IDs: {player_colony_ids}")
print(f"Capital Planet ID: {capital_planet_id}")

# 2. Find matching planets
pmgr = rp(base + 0x3113148)
parr = rp(pmgr + 0x18)
pcap = ru32(pmgr + 0x20)

player_planets = []
for p_id in range(pcap):
    p = rp(parr + p_id * 16 + 8)
    if not p: continue
    if ru32(p + 0x4b8) == 0: continue
    c_ptr = rp(p + 0x4b0)
    if not c_ptr: continue
    cid = ru32(c_ptr)
    if cid in player_colony_ids:
        sys_name = read_pdx_string(p + 0x450)
        is_colonizing = (rp(p + 0x510) == 0)
        size = ru32(p + 0x4a4)
        pops = 0 if is_colonizing else 24
        is_cap = (p_id == capital_planet_id)
        player_planets.append({
            'planet_id': p_id,
            'colony_id': cid,
            'sys_name': sys_name,
            'is_capital': is_cap,
            'is_colonizing': is_colonizing,
            'size': size,
            'pops': pops
        })

print(f"\nDiscovered Player Planets ({len(player_planets)}):")
for pl in player_planets:
    print(pl)

# 3. Sectors
game = rp(base + 0x3112A08)
sec_vec = rp(game + 0x7B8)
sec_cnt = ru32(game + 0x7C4)

sec_mgr = rp(base + 0x3112FA8)
sec_arr = rp(sec_mgr + 0x18)
sec_cap = ru32(sec_mgr + 0x20)

core_planets = []
assigned_ids = set()

for i in range(sec_cnt):
    sid = ru32(sec_vec + i * 4)
    slot = sid & 0xFFFFFF
    if slot >= sec_cap: continue
    s_obj = rp(sec_arr + slot * 16 + 8)
    if not s_obj: continue
    ptr = rp(s_obj + 0x140)
    sz = ru32(s_obj + 0x14c)
    if not ptr or sz == 0: continue
    p_in_sec = set(ru32(ptr + k * 4) for k in range(sz))
    if capital_planet_id in p_in_sec:
        # Core sector
        for pl in player_planets:
            if pl['planet_id'] in p_in_sec:
                core_planets.append(pl)
                assigned_ids.add(pl['planet_id'])

frontier_planets = [pl for pl in player_planets if pl['planet_id'] not in assigned_ids]

print("\n=== Sector Structure ===")
print(f"1. Core Sector (count={len(core_planets)}):")
for cp in core_planets:
    print(f"   - Planet {cp['planet_id']}: sys='{cp['sys_name']}', capital={cp['is_capital']}, pops={cp['pops']}")

print(f"2. Frontier Sector (count={len(frontier_planets)}):")
for fp in frontier_planets:
    print(f"   - Planet {fp['planet_id']}: sys='{fp['sys_name']}', colonizing={fp['is_colonizing']}, size={fp['size']}")
