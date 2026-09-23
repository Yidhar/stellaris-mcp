import ctypes, struct

kernel32 = ctypes.WinDLL('kernel32')
h = kernel32.OpenProcess(0x10, False, 73956)
base = 0x7FF75ED50000

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read))
    return struct.unpack('<Q', buf.raw)[0]

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read))
    return struct.unpack('<I', buf.raw)[0]

def extract_str(addr):
    buf = ctypes.create_string_buffer(32)
    read = ctypes.c_size_t()
    if not kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 32, ctypes.byref(read)):
        return ""
    raw = buf.raw
    sz = struct.unpack('<Q', raw[16:24])[0]
    cap = struct.unpack('<Q', raw[24:32])[0]
    if sz == 0 or sz > 500: return ""
    if cap < 16:
        return raw[:min(sz, 15)].decode('utf-8', errors='ignore')
    else:
        ptr = struct.unpack('<Q', raw[:8])[0]
        if ptr and 0x10000 < ptr < 0x7FFFFFFFFFFF:
            sbuf = ctypes.create_string_buffer(min(sz, 256))
            if kernel32.ReadProcessMemory(h, ctypes.c_void_p(ptr), sbuf, min(sz, 256), ctypes.byref(read)):
                return sbuf.raw[:read.value].decode('utf-8', errors='ignore')
    return ""

idler = r64(base + 0x3287900)
sit_view = r64(idler + 0xD30)
print(f"CSituationLogView: 0x{sit_view:X}")

subview_offsets = [
    0x0D0, 0x0D8, 0x0E0, 0x0E8, 0x0F0, 0x0F8, 0x100, 0x108, 0x110
]

for idx, off in enumerate(subview_offsets):
    sub = r64(sit_view + off)
    vt = r64(sub)
    vt_rva = vt - base if base < vt < base + 0x3000000 else 0
    # Check if sub has a CWindow pointer (usually at +8 or +0x18 or +0x78)
    win_names = []
    for w_off in range(0, 0x80, 8):
        cand = r64(sub + w_off)
        if cand and cand > 0x10000:
            name = extract_str(cand + 0x18)
            if name:
                win_names.append(f"+0x{w_off:X}->'{name}'")
    # Also check string inside sub itself
    direct_str = extract_str(sub + 0x18) or extract_str(sub + 0x28)
    print(f"Subview[{idx}] (+0x{off:03X}): 0x{sub:X} (vt=0x{vt_rva:X}) name='{direct_str}' wins={win_names}")
