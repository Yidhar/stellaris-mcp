import ctypes, struct

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory
pid = 104400
base = 0x7ff75ed50000
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read)):
        return struct.unpack('<I', buf.raw)[0]
    return 0

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

leader_mgr = r64(base + 0x3287320)
tbl = r64(leader_mgr + 0x18)
cap = r32(leader_mgr + 0x20)

leaders = []
for i in range(cap):
    ptr = r64(tbl + i * 16 + 8)
    if ptr:
        lid = r32(ptr + 0x20)
        leaders.append((lid, ptr))

print(f"Scanning {len(leaders)} leaders for country_id field...")

for off in range(0, 0x400, 4):
    vals = [r32(ptr + off) for lid, ptr in leaders]
    # Country ID: values should be between 0 and 100, and 0 MUST be present (for player)
    # and should have at least 5 distinct values (multiple countries)
    distinct = set(vals)
    if 0 in distinct and len(distinct) >= 5 and max(distinct) < 150:
        # Ruler (167772193) should have value 0!
        ruler_val = r32(leaders[33][1] + off) # slot 33 is ruler
        print(f"Candidate country_id at +0x{off:03X}: {len(distinct)} distinct values, max={max(distinct)}, ruler_val={ruler_val}, dist={sorted(distinct)[:10]}")

