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

# Inspect DB 0x3112F78
db_f78 = rp64(base + 0x3112F78)
cap_f78 = rp32(db_f78 + 0x20)
arr_f78 = rp64(db_f78 + 0x18)
print(f'DB 0x3112F78 cap={cap_f78}')

for slot in range(min(cap_f78, 50)):
    item_ptr = rp64(arr_f78 + slot * 16 + 8)
    if not item_ptr: continue
    token = rp32(item_ptr + 8)
    
    # Let us search for strings in item_ptr (first 0x600 bytes)
    # Also check colony pointer, solar system pointer, etc.
    # Where is planet name in CPlanet?
    # In Stellaris CPlanet, name string is often at an offset or via a ref.
    colony_id = rp32(item_ptr + 0xc0)
    build_queue_ref = rp32(item_ptr + 0xc4)
    q_880 = rp32(item_ptr + 0x880)
    owner = rp32(item_ptr + 0x98)
    
    # Check if there's any string inside
    found_strs = []
    for off in range(0, 0x500, 8):
        s = read_pdx_string(item_ptr + off)
        if len(s) > 1 and all(32 <= ord(c) < 127 for c in s):
            found_strs.append((hex(off), s))
            
    print(f'slot {slot:2d} (id {token}): owner={owner}, colony_id={hex(colony_id)}, queue_c4={build_queue_ref}, queue_880={q_880}, strs={found_strs[:3]}')
