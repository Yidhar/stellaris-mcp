import sys
sys.path.insert(0, 'scripts')
import inject, reload_dll
import ctypes

pid = inject.find_stellaris_pid()
h_proc = ctypes.windll.kernel32.OpenProcess(0x1F0FFF, False, pid)
base = reload_dll.find_module(pid, 'stellaris.exe')

def read_u64(addr):
    val = ctypes.c_uint64()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 8, None)
    return val.value

def read_u32(addr):
    val = ctypes.c_uint32()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 4, None)
    return val.value

def read_i64(addr):
    val = ctypes.c_int64()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 8, None)
    return val.value

def read_bytes(addr, count):
    buf = (ctypes.c_char * count)()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, count, None)
    return buf.raw

db_ptr = read_u64(base + 0x3152658)
print('db_ptr:', hex(db_ptr))
arr_ptr = read_u64(db_ptr + 0x10)
count = read_u32(db_ptr + 0x1C)
print(f'count: {count}, arr_ptr: {hex(arr_ptr)}')

non_zero = []
for i in range(count):
    elem = read_u64(arr_ptr + i * 8)
    s_ptr = elem + 0x148
    cap = read_u64(s_ptr + 0x18)
    sz = read_u64(s_ptr + 0x10)
    name = ''
    if cap < 16:
        name = read_bytes(s_ptr, min(sz, 15)).decode('latin1', errors='ignore')
    else:
        hp = read_u64(s_ptr)
        if 0x10000 < hp < 0x7FFFFFFFFFFF:
            name = read_bytes(hp, min(sz, 64)).decode('latin1', errors='ignore')
    raw_cost = read_i64(elem + 0x1B0)
    cost = raw_cost // 100000
    if cost != 0:
        non_zero.append((i, name, cost, elem))

print(f'Total non-zero traits: {len(non_zero)}')
for i, name, cost, elem in non_zero[:25]:
    print(f'Trait {i:3d}: name="{name:30s}" cost={cost:2d} obj={hex(elem)}')

ctypes.windll.kernel32.CloseHandle(h_proc)
