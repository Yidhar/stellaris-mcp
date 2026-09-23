import ctypes, struct

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory
pid = 104400
base = 0x7ff75ed50000
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    bytesRead = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(bytesRead)):
        return buf.raw
    return None

def r64(addr):
    raw = read_bytes(addr, 8)
    return struct.unpack('<Q', raw)[0] if raw else 0

def r32(addr):
    raw = read_bytes(addr, 4)
    return struct.unpack('<I', raw)[0] if raw else 0

leader_mgr = r64(base + 0x3287320)
tbl = r64(leader_mgr + 0x18)

def get_lp(lid):
    p = r64(tbl + (lid & 0xFFFF) * 16 + 8)
    if p and r32(p + 0x20) == lid:
        return p
    return None

p_p93 = get_lp(93)   # Player leader
p_a117 = get_lp(117) # AI leader
p_a123 = get_lp(123) # AI leader

raw_p = read_bytes(p_p93, 0x400)
raw_117 = read_bytes(p_a117, 0x400)
raw_123 = read_bytes(p_a123, 0x400)

print(f"{'Offset':<8} | {'Player (93)':<12} | {'AI (117)':<12} | {'AI (123)':<12} | Notes")
print("-" * 65)

for off in range(0, 0x400, 4):
    up = struct.unpack('<I', raw_p[off:off+4])[0]
    u117 = struct.unpack('<I', raw_117[off:off+4])[0]
    u123 = struct.unpack('<I', raw_123[off:off+4])[0]
    
    # Check if up == 0 and u117 == u123 (or small non-zero)
    if up == 0 and 0 < u117 < 100 and 0 < u123 < 100:
        print(f"+0x{off:03X}   | {up:<12} | {u117:<12} | {u123:<12} | CANDIDATE COUNTRY ID!")
    elif up == 0 and (u117 != 0 or u123 != 0) and off < 0x200:
        # maybe country id?
        if u117 < 100 and u123 < 100:
            print(f"+0x{off:03X}   | {up:<12} | {u117:<12} | {u123:<12} | potential country")

