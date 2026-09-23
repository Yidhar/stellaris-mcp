import ctypes, struct, sys
sys.stdout.reconfigure(encoding='utf-8')

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory
pid = 104400
base = 0x7ff75ed50000
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read)):
        return struct.unpack('<I', buf.raw)[0]
    return 0

def read_pdx_str(addr):
    cap = r64(addr + 0x18)
    size = r64(addr + 0x10)
    if size == 0 or size > 1024: return ""
    buf = ctypes.create_string_buffer(min(size, 256))
    read = ctypes.c_size_t()
    if cap < 16:
        ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, min(size, 15), ctypes.byref(read))
        return buf.raw[:read.value].decode('utf-8', errors='replace')
    else:
        ptr = r64(addr)
        if ptr:
            ReadProcessMemory(hProc, ctypes.c_void_p(ptr), buf, min(size, 256), ctypes.byref(read))
            return buf.raw[:read.value].decode('utf-8', errors='replace')
    return ""

country_mgr = r64(base + 0x3287788)
arr = r64(country_mgr + 0x18)
player_country = r64(arr + 8)

l_arr = r64(player_country + 0x2910)
cnt = r32(player_country + 0x291C)
leader_capacity = r32(player_country + 0x2CB0 + 0x50)

leader_mgr = r64(base + 0x3287320)
tbl = r64(leader_mgr + 0x18)
leader_cap = r32(leader_mgr + 0x20)

print(f"Player country has {cnt} hired leaders (Capacity: {leader_capacity}):")

for i in range(cnt):
    lid = r32(l_arr + i * 4)
    slot = lid & 0xFFFF
    lp = r64(tbl + slot * 16 + 8)
    if lp and r32(lp + 0x20) == lid:
        name_key = read_pdx_str(lp + 0x50)
        level = r32(lp + 0xD0)
        age = r32(lp + 0x108)
        cls_ptr = r64(lp + 0xE0)
        cls_name = read_pdx_str(cls_ptr + 0x20) if cls_ptr else ""
        subcls_ptr = r64(lp + 0x6D0)
        subcls = read_pdx_str(subcls_ptr + 0x20) if subcls_ptr else ""
        ethic_ptr = r64(lp + 0x6D8)
        ethic = read_pdx_str(ethic_ptr + 0x20) if ethic_ptr else ""
        atype = r32(lp + 0x110) & 0xFF
        atarget = r32(lp + 0x118)
        print(f"  [{i}] ID={lid:<10} | Name='{name_key:<32}' | Class={cls_name:<10} | Sub={subcls:<10} | Lv={level} | Age={age} | Ethic={ethic:<18} | Assign={atype}:{atarget}")

