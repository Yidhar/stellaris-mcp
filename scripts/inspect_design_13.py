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

design_mgr = r64(base + 0x3112980)
global_arr = r64(design_mgr + 0x18)

# Design 13 is corvette Kastane
slot13 = r64(global_arr + 13 * 16 + 8)
print(f"CShipDesign[13] at 0x{slot13:X}:")
print(f"  Name: '{read_std_string(slot13 + 0x50)}'")

p_sub = r64(slot13 + 0x20)
p_size = r64(p_sub + 0x08)
print(f"  Ship Size: '{read_std_string(p_size + 0x20)}'")

# Check all floats/ints on CShipDesign (power, military power, etc.)
raw = read_bytes(slot13, 0x208)
print("Numeric values in CShipDesign:")
for off in range(0, 0x208, 4):
    u = struct.unpack('<I', raw[off:off+4])[0]
    flt = struct.unpack('<f', raw[off:off+4])[0]
    if 0 < u < 100000 and (abs(flt) > 0.01 and abs(flt) < 10000):
        print(f"  +0x{off:03X}: int={u}, float={flt:.3f}")

# Sections
sec_arr = r64(p_sub + 0x18)
sec_cnt = r32(p_sub + 0x20)
print(f"\nSections ({sec_cnt}):")
for s_idx in range(sec_cnt):
    p_sec = r64(sec_arr + s_idx * 0x80)
    sec_key = read_std_string(p_sec + 0x18)
    print(f"  [{s_idx}] Template: 0x{p_sec:X}, Key: '{sec_key}'")

# Components
comp_arr = r64(p_sub + 0x30)
comp_cnt = r32(p_sub + 0x38)
print(f"\nComponents ({comp_cnt}):")
for c_idx in range(comp_cnt):
    p_comp = r64(comp_arr + c_idx * 8)
    # Component template name:
    # In earlier dump: p_comp + 0x18 -> 0x26FFA985820 -> +0x20: 'ship_components'
    # And at p_comp + 0x40 -> 0x27023DF3980 -> +0x30: 'GFX_ship_part_reactor_1'
    # Let's check std_string at all offsets of p_comp
    comp_name = ""
    for off in range(0, 0x80, 8):
        s = read_std_string(p_comp + off)
        if s and not s.startswith(""):
            comp_name += f"+0x{off:X}:'{s}' "
    # Also check if p_comp points to component template
    p_tmpl = r64(p_comp + 0x40)
    tmpl_key = ""
    if p_tmpl:
        for off in range(0, 0x60, 8):
            s = read_std_string(p_tmpl + off)
            if s and not s.startswith(""):
                tmpl_key += f"+0x{off:X}:'{s}' "
    print(f"  [{c_idx}] ptr=0x{p_comp:X}: {comp_name} | tmpl=0x{p_tmpl:X}: {tmpl_key}")
