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

country_mgr = r64(base + 0x3287788)
arr = r64(country_mgr + 0x18)
player_country = r64(arr + 8)

leader_mgr = r64(base + 0x3287320)
tbl = r64(leader_mgr + 0x18)
leader_cap = r32(leader_mgr + 0x20)

def is_valid_leader(lid):
    slot = lid & 0xFFFF
    if slot < leader_cap:
        p = r64(tbl + slot * 16 + 8)
        if p and r32(p + 0x20) == lid:
            return True
    return False

print(f"Scanning vectors on player_country (0x{player_country:X})...")

for off in range(0x1800, 0x3500, 8):
    p = r64(player_country + off)
    cnt = r32(player_country + off + 8)
    cap = r32(player_country + off + 12)
    # Check if p is heap and 1 <= cnt <= cap <= 1000
    if 0x17000000000 <= p <= 0x18000000000 and 1 <= cnt <= cap <= 200:
        # Check first element of buffer
        # Could it be uint32 leader IDs?
        buf = ctypes.create_string_buffer(min(cnt * 4, 256))
        read = ctypes.c_size_t()
        if ReadProcessMemory(hProc, ctypes.c_void_p(p), buf, min(cnt * 4, 256), ctypes.byref(read)):
            raw = buf.raw[:read.value]
            u32s = [struct.unpack('<I', raw[i:i+4])[0] for i in range(0, len(raw), 4)]
            # Check if any u32 is a valid leader ID!
            leaders_found = [x for x in u32s if is_valid_leader(x)]
            extra = f" -> Leaders! {leaders_found}" if leaders_found else ""
            print(f"  +0x{off:04X}: ptr=0x{p:X}, cnt={cnt}, cap={cap}{extra}")

