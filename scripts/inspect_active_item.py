import sys
sys.path.append(r'D:\stellarismcp\scripts')
import ctypes, reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
h_proc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def rp(a):
    v = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 8, None)
    return v.value

def ru32(a):
    v = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 4, None)
    return v.value

def read_pdx_string(addr):
    cap = rp(addr + 24)
    sz = rp(addr + 16)
    if sz == 0 or sz > 512: return ''
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = rp(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

q_items = 0x2B46E615240
item_id = ru32(q_items)
print(f'Queue item 0 ID: {item_id} (0x{item_id:X})')

mgr_ea8 = rp(base + 0x3112EA8)
arr_ea8 = rp(mgr_ea8 + 0x18)
item_obj = rp(arr_ea8 + (item_id & 0xFFFFFF) * 16 + 8)
print(f'item_obj in mgr_ea8: 0x{item_obj:X}')

prog = ru32(item_obj + 0x28)
tot = ru32(item_obj + 0x30)
if tot > 0:
    print(f'Progress: {prog} / {tot} ({(prog / tot * 100):.1f}%)')
else:
    print(f'Progress: {prog} / {tot}')

for off in range(0, 0x150, 8):
    s = read_pdx_string(item_obj + off)
    if s:
        print(f'  +0x{off:X} str: "{s}"')

for off in range(0, 0x80, 4):
    v = ru32(item_obj + off)
    if 0 < v < 1000000:
        print(f'  +0x{off:X} u32: {v}')
