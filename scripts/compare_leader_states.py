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
leader_cap = r32(leader_mgr + 0x20)

def get_lp(lid):
    p = r64(tbl + (lid & 0xFFFF) * 16 + 8)
    if p and r32(p + 0x20) == lid:
        return p
    return None

p_ruler = get_lp(167772193)
p_hired = get_lp(93)
p_cand = get_lp(81)

print(f"Ruler ptr: {hex(p_ruler)}")
print(f"Hired (93) ptr: {hex(p_hired)}")
print(f"Candidate (81) ptr: {hex(p_cand)}")

raw_r = read_bytes(p_ruler, 0x400)
raw_h = read_bytes(p_hired, 0x400)
raw_c = read_bytes(p_cand, 0x400)

print("\nComparing Ruler, Hired, Candidate:")
print(f"{'Offset':<8} | {'Ruler':<12} | {'Hired (93)':<12} | {'Cand (81)':<12} | Notes")
print("-" * 65)

for off in range(0, 0x400, 4):
    ur = struct.unpack('<I', raw_r[off:off+4])[0]
    uh = struct.unpack('<I', raw_h[off:off+4])[0]
    uc = struct.unpack('<I', raw_c[off:off+4])[0]
    
    # Highlight similarities between Ruler & Hired that differ from Candidate
    notes = ""
    if ur == uh and ur != uc:
        notes = f"R==H != C (val={ur} vs {uc})"
    elif uh == 0 and uc != 0:
        notes = f"H=0, C={uc}"
    elif off in [0x20, 0x50, 0xD0, 0xE0, 0x108, 0x110, 0x114, 0x118, 0x218, 0x230]:
        notes = f"known field"
    
    if notes:
        print(f"+0x{off:03X}   | {ur:<12} | {uh:<12} | {uc:<12} | {notes}")

