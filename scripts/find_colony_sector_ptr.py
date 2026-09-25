import sys, ctypes
sys.path.append(r'D:\stellarismcp\scripts')
import reload_dll, inject

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

colony_mgr = rp(base + 0x3113148)
colony_arr = rp(colony_mgr + 0x18)

# Sector 0 address is 0x2B460396040, sector id is 0
# Let's check which planets have sector id = 0 or pointer to Sector 0!
test_ids = [11, 12, 31, 54, 63, 76, 82, 87, 118, 144, 149, 161, 173]

for cid in test_ids:
    slot = cid & 0xFFFFFF
    c_obj = rp(colony_arr + slot * 16 + 8)
    # Search for 0x2B460396040 or sector ID 0
    ptrs = []
    for off in range(0, 0x500, 8):
        v = rp(c_obj + off)
        if v == 0x2B460396040:
            ptrs.append(f"+0x{off:X} -> Sector 0")
    # Check what sector pointer / ID is in c_obj
    print(f"Colony {cid}: {ptrs}")
