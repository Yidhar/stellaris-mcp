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
        if ptr and 0x10000 < ptr < 0x7FFFFFFFFFFF:
            ReadProcessMemory(hProc, ctypes.c_void_p(ptr), buf, min(size, 256), ctypes.byref(read))
            return buf.raw[:read.value].decode('utf-8', errors='replace')
    return ""

leader_mgr = r64(base + 0x3287320)
tbl = r64(leader_mgr + 0x18)

lid = 134217755
slot = lid & 0xFFFF
lp = r64(tbl + slot * 16 + 8)

print(f"Scanning leader {lid} (0x{lp:X}) for names and strings:")
for off in range(0, 0x500, 8):
    s = read_pdx_str(lp + off)
    if s:
        print(f"  PdxStr at +{hex(off)}: '{s}'")
    q = r64(lp + off)
    if 0x17000000000 <= q <= 0x18000000000:
        qs = read_pdx_str(q)
        if qs:
            print(f"  Ptr at +{hex(off)} -> 0x{q:X}: '{qs}'")

