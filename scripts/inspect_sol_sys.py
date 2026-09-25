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

db_148 = rp64(base + 0x3113148)
arr_148 = rp64(db_148 + 0x18)
sol_sys = rp64(arr_148 + 11 * 16 + 8)

# Check all pointers and arrays in sol_sys
print('=== Sol System (0x229e36a18d0) ===')
for off in range(0, 0x600, 8):
    val64 = rp64(sol_sys + off)
    val32 = rp32(sol_sys + off)
    # Check if val64 looks like a heap pointer with an array
    # In Stellaris CPdxArray has (data_ptr, capacity, size) or (capacity, size)
    # Let's check if val64 is a valid pointer
    if 0x20000000000 <= val64 <= 0x24000000000:
        # Check if it has planets
        cnt = rp32(sol_sys + off + 8)
        print(f'  +0x{off:03x}: ptr={hex(val64)}, cnt/next={cnt}')
        # Check first few entries
        entries = [rp32(val64 + i * 4) for i in range(min(cnt, 16))]
        print(f'    entries: {entries}')
