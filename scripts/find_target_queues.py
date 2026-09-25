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

matches = []
for slot in range(cap_eb8):
    q_obj = rp64(arr_eb8 + slot * 16 + 8)
    if not q_obj: continue
    token = rp32(q_obj + 8)
    owner = rp32(q_obj + 0x30)
    target_type = rp32(q_obj + 0x40)
    target_id = rp32(q_obj + 0x44)
    qtype = rp32(q_obj + 0x4c) & 0xFF
    cnt = rp32(q_obj + 0x2c)
    if target_id == 11 or target_id == 0 or target_id == 287:
        matches.append((slot, token, owner, target_type, target_id, qtype, cnt))

print(f'Queues matching target_id in (0, 11, 287): {len(matches)}')
for m in matches:
    print(f'  slot {m[0]} (token {m[1]}): owner={m[2]}, target_type={m[3]}, target_id={m[4]}, qtype={m[5]}, cnt={m[6]}')
