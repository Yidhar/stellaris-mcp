import ctypes

PROCESS_ALL_ACCESS = 0x1F0FFF
kernel32 = ctypes.windll.kernel32
pid = 67160
hProcess = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
base = 0x7FF777BD0000

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    r = ctypes.c_size_t(0)
    return buf.raw if kernel32.ReadProcessMemory(hProcess, ctypes.c_void_p(addr), buf, size, ctypes.byref(r)) else None

def read_u64(addr):
    b = read_bytes(addr, 8)
    return int.from_bytes(b, 'little') if b else 0

def read_u32(addr):
    b = read_bytes(addr, 4)
    return int.from_bytes(b, 'little') if b else 0

# Get player country
mgr = read_u64(base + 0x3112F50)
arr = read_u64(mgr + 0x18)
player_country = read_u64(arr + 8)

# Read fleet templates vector at player_country + 0x2648
vec_ptr = read_u64(player_country + 0x2648 + 8)
vec_cnt = read_u32(player_country + 0x2648 + 0x14)
print(f"Player has {vec_cnt} fleet templates")

fleet_template_mgr = read_u64(base + 0x3113038)
ft_arr = read_u64(fleet_template_mgr + 0x18)
ft_cap = read_u32(fleet_template_mgr + 0x20)

for i in range(vec_cnt):
    template_id = read_u32(vec_ptr + i * 4)
    print(f"=== Template #{i} (ID: {template_id}) ===")
    ft_obj = read_u64(ft_arr + (template_id & 0xFFFFFF) * 16 + 8)
    if not ft_obj: continue
    
    # Read designs array at ft_obj + 0x28, count at ft_obj + 0x34
    designs_arr = read_u64(ft_obj + 0x28)
    designs_cnt = read_u32(ft_obj + 0x34)
    associated_fleet_id = read_u32(ft_obj + 0x88)
    print(f"  Associated Fleet ID: {associated_fleet_id}, Designs count: {designs_cnt}")
    
    for d in range(designs_cnt):
        d_entry = designs_arr + d * 0x560
        actual_count = read_u32(d_entry + 0x1C)
        target_quota = read_u32(d_entry + 0x558)
        design_id = read_u32(d_entry + 0x28)
        ship_design_ptr = read_u64(d_entry + 0x40)
        
        # Read ship design name from ship_design_ptr + 0x20
        design_name = "unknown"
        if ship_design_ptr:
            sz = read_u64(ship_design_ptr + 0x20 + 16)
            cp = read_u64(ship_design_ptr + 0x20 + 24)
            src = ship_design_ptr + 0x20 if cp < 16 else read_u64(ship_design_ptr + 0x20)
            raw = read_bytes(src, sz)
            if raw:
                design_name = raw.decode("utf-8", errors="replace")
        
        print(f"  [Design {d}]: ID={design_id}, Name='{design_name}', Actual={actual_count}, TargetQuota={target_quota}")
