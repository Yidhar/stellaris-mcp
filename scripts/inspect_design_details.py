import sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.append('scripts')
import ctypes, struct
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(read)):
        return buf.raw[:read.value]
    return b''

def r64(addr):
    raw = read_bytes(addr, 8)
    return struct.unpack('<Q', raw)[0] if len(raw) == 8 else 0

def r32(addr):
    raw = read_bytes(addr, 4)
    return struct.unpack('<I', raw)[0] if len(raw) == 4 else 0

def read_std_string(addr):
    cap = r64(addr + 0x18)
    sz = r64(addr + 0x10)
    if sz == 0 or sz > 500: return ''
    if cap < 16:
        buf = ctypes.create_string_buffer(sz)
        read = ctypes.c_size_t()
        kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, sz, ctypes.byref(read))
        return buf.raw[:read.value].decode('utf-8', errors='replace')
    else:
        ptr = r64(addr)
        if ptr < 0x10000 or ptr > 0x7FFFFFFFFFFF: return ''
        buf = ctypes.create_string_buffer(sz)
        read = ctypes.c_size_t()
        kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(ptr), buf, sz, ctypes.byref(read))
        return buf.raw[:read.value].decode('utf-8', errors='replace')

p0 = 0x27065D99100

print(f"CShipDesign: 0x{p0:X}")
print(f"  Name: {read_std_string(p0 + 0x50)}")
print(f"  Class Name Key: {read_std_string(p0 + 0xA8)}")

# Let's inspect +0x20
p_sub = r64(p0 + 0x20)
print(f"  p_sub: 0x{p_sub:X}")

# ShipSize is at p_sub + 0x08?
p_ship_size = r64(p_sub + 0x08)
print(f"  p_ship_size: 0x{p_ship_size:X}")
for off in range(0, 0x80, 8):
    s = read_std_string(p_ship_size + off)
    if s: print(f"    ShipSize string at +0x{off:X}: {s}")

# Sections vector at p_sub + 0x18
sec_arr = r64(p_sub + 0x18)
sec_cnt = r32(p_sub + 0x20)
print(f"  Sections: count={sec_cnt}, arr=0x{sec_arr:X}")
for s_idx in range(sec_cnt):
    p_sec_tmpl = r64(sec_arr + s_idx * 0x80)
    print(f"    Section[{s_idx}]: template=0x{p_sec_tmpl:X}")
    for off in range(0, 0x60, 8):
        s = read_std_string(p_sec_tmpl + off)
        if s: print(f"      Section template str at +0x{off:X}: {s}")

# Components vector at p_sub + 0x30
comp_arr = r64(p_sub + 0x30)
comp_cnt = r32(p_sub + 0x38)
print(f"  Components: count={comp_cnt}, arr=0x{comp_arr:X}")
# Let's check size of each component entry in comp_arr
raw_comp = read_bytes(comp_arr, 0x100)
for c_idx in range(comp_cnt):
    p_comp = r64(comp_arr + c_idx * 8)
    print(f"    Component[{c_idx}]: ptr=0x{p_comp:X}")
    for off in range(0, 0x80, 8):
        s = read_std_string(p_comp + off)
        if s: print(f"      Comp[{c_idx}] str at +0x{off:X}: {s}")
