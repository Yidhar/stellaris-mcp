import sys
sys.path.append('scripts')
import ctypes, inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def read_u64(addr):
    val = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 8, None)
    return val.value

def read_u32(addr):
    val = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 4, None)
    return val.value

def read_pdx_string(addr):
    cap = read_u64(addr + 0x18)
    size = read_u64(addr + 0x10)
    if size == 0: return ''
    if cap >= 16:
        p = read_u64(addr)
    else:
        p = addr
    buf = bytearray(min(size, 128))
    n = ctypes.c_size_t()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(p), (ctypes.c_char * len(buf)).from_buffer(buf), len(buf), ctypes.byref(n))
    return bytes(buf[:n.value]).decode('utf-8', errors='ignore')

def get_design_ptr(did):
    mgr = read_u64(base + 0x3112980)
    arr = read_u64(mgr + 0x18)
    idx = (did & 0xffffff) * 16 + 8
    return read_u64(arr + idx)

p1 = get_design_ptr(50333161) # Python
p2 = get_design_ptr(1599)     # Spectre

print('Python (50333161) at:', hex(p1))
print('Spectre (1599) at:', hex(p2))

out_lines = []

def dump_arr(arr_addr, name):
    out_lines.append(f'=== Array for {name} at {hex(arr_addr)} ===')
    for i in range(4):
        p = arr_addr + i * 0x38
        s = read_pdx_string(p)
        out_lines.append(f'  [{i}] s="{repr(s)}"')

dump_arr(0x2901a410250, 'Python')
dump_arr(0x290713b8000, 'Spectre')

with open('scripts/compare_out.txt', 'w', encoding='utf-8') as fp:
    fp.write('\n'.join(out_lines))
print('Written to scripts/compare_out.txt!')
