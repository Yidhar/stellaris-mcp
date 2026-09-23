import sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.append('scripts')
import ctypes, struct
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(read)):
        return buf.raw[:read.value]
    return b''

def r64(addr):
    raw = read_bytes(addr, 8)
    return struct.unpack('<Q', raw)[0] if len(raw) == 8 else 0

def r32(addr):
    raw = read_bytes(addr, 4)
    return struct.unpack('<I', raw)[0] if len(raw) == 4 else 0

def read_std_string(addr):
    cap = r64(addr + 0x18)
    sz = r64(addr + 0x10)
    if sz == 0 or sz > 500: return ''
    if cap < 16:
        buf = ctypes.create_string_buffer(sz)
        read = ctypes.c_size_t()
        kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, sz, ctypes.byref(read))
        return buf.raw[:read.value].decode('utf-8', errors='replace')
    else:
        ptr = r64(addr)
        if ptr < 0x10000 or ptr > 0x7FFFFFFFFFFF: return ''
        buf = ctypes.create_string_buffer(sz)
        read = ctypes.c_size_t()
        kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(ptr), buf, sz, ctypes.byref(read))
        return buf.raw[:read.value].decode('utf-8', errors='replace')

mgr = r64(base + 0x3112F50)
countries_arr = r64(mgr + 0x18)
country = r64(countries_arr + 8)

arr_ptr = r64(country + 0x1AD0)
count = r32(country + 0x1ADC)

print(f"Country: 0x{country:X}")
print(f"Design vector: ptr=0x{arr_ptr:X}, count={count}")

design_mgr = r64(base + 0x3112980)
cap = r32(design_mgr + 0x20)
global_arr = r64(design_mgr + 0x18)

for i in range(count):
    did = r32(arr_ptr + i * 4)
    slot = r64(global_arr + (did & 0xFFFFFF) * 16 + 8)
    name = read_std_string(slot + 0x50)
    p_sub = r64(slot + 0x20)
    p_size = r64(p_sub + 0x08) if p_sub else 0
    size_name = read_std_string(p_size + 0x20) if p_size else ''
    print(f"  [{i:2d}] design_id={did:4d}, size={size_name:15s}, name='{name}'")
