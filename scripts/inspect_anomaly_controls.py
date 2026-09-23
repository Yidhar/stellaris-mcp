import ctypes
from ctypes import wintypes

pid = 99112
h_proc = ctypes.windll.kernel32.OpenProcess(0x1F0FFF, False, pid)
live_base = 0x7FF777BD0000

def read_u64(addr):
    buf = ctypes.c_uint64()
    read = ctypes.c_size_t()
    if ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(buf), 8, ctypes.byref(read)):
        return buf.value
    return 0

def read_u32(addr):
    buf = ctypes.c_uint32()
    read = ctypes.c_size_t()
    if ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(buf), 4, ctypes.byref(read)):
        return buf.value
    return 0

def read_bytes(addr, sz):
    buf = (ctypes.c_char * sz)()
    read = ctypes.c_size_t()
    if ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, ctypes.byref(read)):
        return bytes(buf)
    return b''

def read_pdx_str(addr):
    raw = read_bytes(addr, 32)
    if not raw: return ''
    sz = int.from_bytes(raw[16:24], 'little')
    cap = int.from_bytes(raw[24:32], 'little')
    if sz == 0: return ''
    if cap < 16:
        return raw[:sz].decode('latin-1', errors='ignore')
    ptr = int.from_bytes(raw[:8], 'little')
    if 0x10000 < ptr < 0x7FFFFFFFFFFF:
        return read_bytes(ptr, min(sz, 200)).decode('latin-1', errors='ignore')
    return ''

idler = read_u64(live_base + 0x3113180)
anom = read_u64(idler + 0xB08)
print(f'anom: 0x{anom:X}')
ui_win = read_u64(anom + 0x78)
print(f'ui_win: 0x{ui_win:X}')

seen = set()

def explore_container(c, depth=0):
    if not c or c in seen: return
    seen.add(c)
    prefix = '  ' * depth
    vt = read_u64(c)
    # Check children map
    keys_arr = read_u64(c + 0x710)
    cnt = read_u32(c + 0x71C)
    vals_arr = read_u64(c + 0x6F8)
    if keys_arr and vals_arr and 0 < cnt < 100:
        for i in range(cnt):
            k = read_pdx_str(keys_arr + i * 48 + 16)
            child = read_u64(vals_arr + i * 8)
            cvt = read_u64(child)
            print(f'{prefix}[Control] "{k}" -> 0x{child:X} (vt: 0x{cvt - live_base:X})')
            if 'research' in k.lower() or 'ok' in k.lower():
                print(f'{prefix}   *** Found button: {k} at 0x{child:X} ***')
                for off in range(0, 0x200, 8):
                    val = read_u64(child + off)
                    if live_base < val < live_base + 0x2800000:
                        print(f'{prefix}       +0x{off:X}: 0x{val:X} (RVA: 0x{val - live_base:X})')
            explore_container(child, depth + 1)

    # Check sub containers array
    c_arr = read_u64(c + 0x878)
    c_cnt = read_u32(c + 0x884)
    if c_arr and 0 < c_cnt < 50:
        for j in range(c_cnt):
            sub = read_u64(c_arr + j * 8)
            explore_container(sub, depth + 1)

explore_container(ui_win)
