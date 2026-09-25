import ctypes
import sys
sys.path.append(r'D:\stellarismcp\scripts')
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = ctypes.windll.kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def rp64(addr):
    v = ctypes.c_uint64()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(v), 8, None)
    return v.value

def rp32(addr):
    v = ctypes.c_uint32()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(v), 4, None)
    return v.value

db_eb8 = rp64(base + 0x3112EB8)
arr_eb8 = rp64(db_eb8 + 0x18)
cap_eb8 = rp32(db_eb8 + 0x20)
print(f'Total queues in DB: {cap_eb8}')

player_queues = []
for slot in range(cap_eb8):
    q_obj = rp64(arr_eb8 + slot * 16 + 8)
    if not q_obj: continue
    token = rp32(q_obj + 8)
    owner = rp32(q_obj + 0x30)
    carrier = rp32(q_obj + 0x44)
    qtype = rp32(q_obj + 0x4c) & 0xFF
    cnt = rp32(q_obj + 0x2c)
    if owner == 0 or owner == 287 or owner == 17:
        player_queues.append((slot, token, owner, carrier, qtype, cnt))

print(f'Queues matching owner 0, 17, or 287: {len(player_queues)}')
for slot, token, owner, carrier, qtype, cnt in player_queues[:30]:
    print(f'  slot {slot} (token {token}): owner_30={owner}, carrier_44={carrier}, qtype_4c={qtype}, count_2c={cnt}')
