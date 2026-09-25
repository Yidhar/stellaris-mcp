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

# Earth colony:
cmgr = rp(base + 0x3113140)
arr = rp(cmgr + 0x18)
s0 = rp(arr + 8) # Earth
print(f'Earth colony s0: 0x{s0:X}')

# In 0x14AA8A0:
# mov edx, dword ptr [rbx + 0xc0] (rbx = s0)
# or if flag at [rax + 0x128] + 0x510 >> 18 & 1: edx = [rax + 0xc4]
# For Earth, s0 + 0xc0 is 1.
# Manager at base + 0x3112EB8:
mgr_eb8 = rp(base + 0x3112EB8)
arr_eb8 = rp(mgr_eb8 + 0x18)
cap_eb8 = ru32(mgr_eb8 + 0x20)
print(f'mgr_eb8: 0x{mgr_eb8:X}, cap: {cap_eb8}')

# Lookup slot 1 in mgr_eb8:
slot1 = rp(arr_eb8 + 1 * 16 + 8)
print(f'Slot 1 obj in mgr_eb8: 0x{slot1:X}')

# Check [rcx + 0x2c] and [rcx + 0x20]:
queue_cnt = ru32(slot1 + 0x2c)
queue_items_ptr = rp(slot1 + 0x20)
print(f'Queue count: {queue_cnt}, Queue items ptr: 0x{queue_items_ptr:X}')

# Manager at base + 0x3112EA8:
mgr_ea8 = rp(base + 0x3112EA8)
arr_ea8 = rp(mgr_ea8 + 0x18)
cap_ea8 = ru32(mgr_ea8 + 0x20)
print(f'mgr_ea8: 0x{mgr_ea8:X}, cap: {cap_ea8}')

if queue_cnt > 0 and queue_items_ptr:
    for i in range(queue_cnt):
        item_id = ru32(queue_items_ptr + i * 4)
        print(f'  Item {i}: ID = {item_id} (0x{item_id:X})')
        slot_item = item_id & 0xFFFFFF
        if slot_item < cap_ea8:
            item_obj = rp(arr_ea8 + slot_item * 16 + 8)
            print(f'    Item obj: 0x{item_obj:X}')
            # Dump strings and fields in item_obj
            for off in range(0, 0x150, 8):
                s = read_pdx_string(item_obj + off)
                if s:
                    print(f'      +0x{off:X} string: "{s}"')
            for off in range(0, 0x80, 4):
                val = ru32(item_obj + off)
                if 0 < val < 100000:
                    print(f'      +0x{off:X} u32: {val}')
