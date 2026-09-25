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

# Let's search memory for any object that has:
# string 'planet_or_starbase_status_entry' OR has +0x68 in [0..14]
# Earlier we saw that each item in planet_status_list has vtable at 0x14D9800:
# lea rax, [rip + 0xe70a29] -> 0x14D9800 + 7 + 0xE70A29 = 0x234A230!
item_vt = base + 0x14D9800 + 7 + 0x0E70A29
print(f'planet_or_starbase_status_entry vtable: 0x{item_vt - base:X}')

# Let's scan heap for pointers to item_vt!
# Or let's scan for references to base + 0x234A230!
mbi = ctypes.create_string_buffer(48)
addr = 0x10000000000
found = []
target_bytes = item_vt.to_bytes(8, 'little')

while kernel32.VirtualQueryEx(h_proc, ctypes.c_void_p(addr), mbi, 48):
    base_a = ctypes.c_uint64.from_buffer_copy(mbi[0:8]).value
    sz = ctypes.c_uint64.from_buffer_copy(mbi[24:32]).value
    state = ctypes.c_uint32.from_buffer_copy(mbi[32:36]).value
    protect = ctypes.c_uint32.from_buffer_copy(mbi[36:40]).value
    if state == 0x1000 and (protect & 0x04 or protect & 0x02): # PAGE_READWRITE
        chunk_sz = min(sz, 10*1024*1024)
        buf = (ctypes.c_char * chunk_sz)()
        read = ctypes.c_size_t()
        if kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(base_a), buf, chunk_sz, ctypes.byref(read)):
            raw = bytes(buf[:read.value])
            pos = 0
            while True:
                idx = raw.find(target_bytes, pos)
                if idx == -1: break
                entry_addr = base_a + idx
                case_idx = ru32(entry_addr + 0x68)
                found.append((entry_addr, case_idx))
                pos = idx + 8
    addr = base_a + sz
    if addr > 0x30000000000: break

print(f'Found {len(found)} status entries in entire memory:')
for ea, c_idx in found:
    print(f'  Addr: 0x{ea:X}, case_idx: {c_idx}')
