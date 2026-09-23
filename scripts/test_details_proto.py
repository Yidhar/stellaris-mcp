import sys; sys.path.append('scripts')
import ctypes, json, inject, reload_dll

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

def read_i32(addr):
    buf = ctypes.c_int32()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 4, None)
    return buf.value

def read_u8(addr):
    buf = ctypes.c_uint8()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 1, None)
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

slot_size_map = {
    1: 'small',
    2: 'medium',
    3: 'large',
    4: 'torpedo',
    5: 'extra_large',
    6: 'titanic',
    7: 'planet_killer',
    8: 'hangar',
    9: 'aux'
}

modifier_name_map = {
    30: 'hull_add',
    34: 'hull_regen',
    36: 'armor_add',
    41: 'armor_regen',
    46: 'shield_add',
    51: 'shield_regen',
    59: 'weapon_range_mult',
    64: 'fire_rate_mult',
    73: 'evasion_add',
    74: 'evasion_mult',
    75: 'accuracy_add',
    79: 'base_speed_mult',
    80: 'speed_mult'
}

def get_component_details(query):
    query_upper = query.strip().upper()
    target_set = None

    # Step 1: match set_key directly
    for i in range(cnt):
        set_ptr = read_u64(arr + i * 8)
        sk = read_pdx_string(set_ptr + 0x18).upper()
        if sk == query_upper:
            target_set = set_ptr
            break

    # Step 2: match variant component_key
    if not target_set:
        for i in range(cnt):
            set_ptr = read_u64(arr + i * 8)
            c_vec = read_u64(set_ptr + 0xB8)
            c_cnt = read_u64(set_ptr + 0xC0) & 0xFFFFFFFF
            for j in range(c_cnt):
                tmpl = read_u64(c_vec + j * 8)
                tk = read_pdx_string(tmpl + 0x1B0).upper()
                if tk == query_upper:
                    target_set = set_ptr
                    break
            if target_set: break

    if not target_set:
        return {"error": f"Component or component set not found: {query}"}

    set_key = read_pdx_string(target_set + 0x18)
    loc_name = read_pdx_string(target_set + 0x50)
    icon_gfx = read_pdx_string(target_set + 0x80)

    c_vec = read_u64(target_set + 0xB8)
    c_cnt = read_u64(target_set + 0xC0) & 0xFFFFFFFF

    variants = []
    for j in range(c_cnt):
        tmpl = read_u64(c_vec + j * 8)
        t_key = read_pdx_string(tmpl + 0x1B0)
        pwr = read_i64(tmpl + 0x2A8) / 100000.0
        s_enum = read_u8(tmpl + 0x1E0)
        s_name = slot_size_map.get(s_enum, 'standard')
        if s_name == 'standard':
            if t_key.startswith('SMALL_'): s_name = 'small'
            elif t_key.startswith('MEDIUM_'): s_name = 'medium'
            elif t_key.startswith('LARGE_'): s_name = 'large'
            elif t_key.startswith('AUX_'): s_name = 'aux'
            elif 'TITAN' in t_key or 'ION' in t_key: s_name = 'titanic'

        vt = read_u64(tmpl)
        vt_rva = vt - base

        var_info = {
            "component_key": t_key,
            "size": s_name,
            "power": round(pwr, 2)
        }

        if vt_rva == 0x23714F0:
            var_info["type"] = "weapon"
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
            sh_pen = read_i64(tmpl + 0x1240) / 100000.0
            ar_pen = read_i64(tmpl + 0x1248) / 100000.0
            min_w = read_i64(tmpl + 0x11D0) / 100000.0
            max_w = min_w + read_i64(tmpl + 0x11D8) / 100000.0

            var_info["weapon_stats"] = {
                "damage": [round(min_d, 2), round(max_d, 2)],
                "range": round(rng, 1),
                "cooldown": round(cd, 2),
                "accuracy": round(acc, 2),
                "tracking": round(trk, 2),
                "shield_mult": round(shield_m, 2),
                "armor_mult": round(armor_m, 2),
                "hull_mult": round(hull_m, 2),
                "shield_penetration": round(sh_pen, 2),
                "armor_penetration": round(ar_pen, 2),
                "windup": [round(min_w, 2), round(max_w, 2)]
            }
        elif vt_rva == 0x2371380:
            var_info["type"] = "strike_craft"
            c8 = read_i32(tmpl + 0x11C8)
            rng = read_i64(tmpl + 0x11E8) / 100000.0
            d_min = read_i64(tmpl + 0x1250) / 100000.0
            d_max = read_i64(tmpl + 0x1258) / 100000.0
            acc = read_i64(tmpl + 0x11F8) / 100000.0
            trk = read_i64(tmpl + 0x1200) / 100000.0
            cd = read_i64(tmpl + 0x11D8) / 100000.0
            var_info["strike_craft_stats"] = {
                "craft_count": c8,
                "engagement_range": round(rng, 1),
                "damage": [round(d_min, 2), round(d_max, 2)],
                "cooldown": round(cd, 2),
                "accuracy": round(acc, 2),
                "tracking": round(trk, 2)
            }
        elif vt_rva == 0x2371468:
            var_info["type"] = "utility"
            stats = {}
            mod = tmpl + 0x2B0
            p2 = read_u64(mod + 0x38)
            m_cnt = read_u32(mod + 0x44)
            if p2 and m_cnt > 0 and m_cnt < 20:
                for idx in range(m_cnt):
                    val = read_i64(p2 + idx * 16) / 100000.0
                    m_type = read_i64(p2 + idx * 16 + 8)
                    m_name = modifier_name_map.get(m_type, f"mod_{m_type}")
                    stats[m_name] = round(val, 2)

            sr = read_i32(tmpl + 0x11C8)
            hl = read_i32(tmpl + 0x11CC)
            if sr > 0: stats["sensor_range"] = sr
            if hl > 0: stats["hyperlane_range"] = hl

            beh = read_pdx_string(tmpl + 0x11E8)
            if beh: stats["ship_behavior"] = beh

            var_info["utility_stats"] = stats
        else:
            var_info["type"] = f"other_0x{vt_rva:X}"

        variants.append(var_info)

    return {
        "set_key": set_key,
        "localized_name": loc_name,
        "icon": icon_gfx,
        "total_variants": len(variants),
        "variants": variants
    }

queries = ["PLASMA", "PERDITION_BEAM_ION", "DARK_MATTER_DEFLECTOR", "SENSOR_4", "AFTERBURNER_2", "STRIKE_CRAFT"]
for q in queries:
    res = get_component_details(q)
    print(f"\n================ QUERY: {q} ================")
    print(json.dumps(res, indent=2, ensure_ascii=False))

reload_dll.kernel32.CloseHandle(hProc)
