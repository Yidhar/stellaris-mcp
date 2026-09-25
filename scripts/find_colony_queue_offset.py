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

# Colony DB is 0x3113140
c_mgr = rp64(base + 0x3113140)
arr = rp64(c_mgr + 0x18)

pairs = [
    (0, 46),
    (3, 0),
    (6, 51),
    (15, 60),
    (16, 61),
    (17, 62)
]

# For each pair (colony_id, queue_id), check all offsets in colony_obj
candidate_offsets = None

for cid, qid in pairs:
    col_ptr = rp64(arr + cid * 16 + 8)
    if not col_ptr:
        print(f'Colony {cid} is NULL')
        continue
    matching_offsets = set()
    for off in range(0, 0x1500, 4):
        if rp32(col_ptr + off) == qid:
            matching_offsets.add(off)
    print(f'Colony {cid} (Queue {qid}) matches at: {[hex(o) for o in matching_offsets]}')
    if candidate_offsets is None:
        candidate_offsets = matching_offsets
    else:
        candidate_offsets &= matching_offsets

print(f'Intersection of candidate offsets: {[hex(o) for o in candidate_offsets]}')
