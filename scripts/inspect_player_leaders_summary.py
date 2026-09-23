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

# Check player leaders:
# We know the player's candidate recruitment pool has 12 leaders:
# 81..92!
# And player's active leaders:
# Ruler (167772193)
# 93 (Official)
# 97 (Commander)
# 100 (Scientist)
# 103 (Official)
# 212 (Envoy)
# 213 (Envoy)
print("Player leaders:")
for lid in [167772193, 93, 97, 100, 103, 212, 213]:
    slot = lid & 0xFFFF
    leader_mgr = r64(base + 0x3287320)
    tbl = r64(leader_mgr + 0x18)
    lp = r64(tbl + slot * 16 + 8)
    if lp:
        # Check level, age, assignment
        lv = r32(lp + 0xD0)
        age = r32(lp + 0x108)
        atype = r32(lp + 0x110) & 0xFF
        atarget = r32(lp + 0x118)
        cls_ptr = r64(lp + 0xE0)
        cls_name = ""
        if cls_ptr:
            # PdxString at cls_ptr + 0x20
            buf = ctypes.create_string_buffer(32)
            read = ctypes.c_size_t()
            ReadProcessMemory(hProc, ctypes.c_void_p(cls_ptr + 0x20), buf, 32, ctypes.byref(read))
            cls_name = buf.raw[:read.value].split(b'\x00')[0].decode('latin-1', errors='ignore')
        
        # Check traits at +0x750 or +0x768
        t1_cnt = r32(lp + 0x758)
        t2_cnt = r32(lp + 0x770)
        
        print(f"  Leader ID={lid:10d}: Class='{cls_name:10s}', Lv={lv}, Age={age}, AssignType={atype}, Target={atarget}, T1={t1_cnt}, T2={t2_cnt}")

