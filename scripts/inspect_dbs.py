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

for db_off, name in [(0x3112F78, 'DB 0x3112F78'), (0x3113148, 'DB 0x3113148')]:
    ptr = rp64(base + db_off)
    arr = rp64(ptr + 0x18)
    print(f'=== {name} ===')
    for slot in range(15):
        item_ptr = rp64(arr + slot * 16 + 8)
        if not item_ptr: continue
        s_450 = read_pdx_string(item_ptr + 0x450)
        print(f'  slot {slot}: ptr={hex(item_ptr)} s_450="{s_450}"')
