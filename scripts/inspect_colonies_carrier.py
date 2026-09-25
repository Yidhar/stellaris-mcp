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

def read_pdx_string(addr):
    size = rp64(addr + 0x10)
    if size < 16:
        buf = (ctypes.c_char * 16)()
        ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, 16, None)
        return bytes(buf).split(b'\x00')[0].decode('utf-8', errors='ignore')
    else:
        ptr = rp64(addr)
        if not ptr: return ''
        buf = (ctypes.c_char * min(size + 1, 128))()
        ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, len(buf), None)
        return bytes(buf).split(b'\x00')[0].decode('utf-8', errors='ignore')

db_col = rp64(base + 0x3113140)
arr_col = rp64(db_col + 0x18)

db_planet = rp64(base + 0x3113128)
arr_planet = rp64(db_planet + 0x18)

for cid in [0, 28]:
    col = rp64(arr_col + cid * 16 + 8)
    planet_id = rp32(col + 0xf78)
    carrier_type = rp32(col + 0xf70)
    planet_ptr = rp64(arr_planet + planet_id * 16 + 8) if planet_id != 0xFFFFFFFF else 0
    pname = read_pdx_string(planet_ptr + 0x108) if planet_ptr else 'NONE'
    qid = rp32(planet_ptr + 0xe4) if planet_ptr else -1
    print(f'Colony {cid}: carrier_type={carrier_type}, planet_id={planet_id}, planet_name="{pname}", queue_id={qid}')
