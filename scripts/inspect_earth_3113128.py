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

db = rp64(base + 0x3113128)
arr = rp64(db + 0x18)
earth_planet = rp64(arr + 3 * 16 + 8)

print(f'Earth planet at {hex(earth_planet)}')
name = read_pdx_string(earth_planet + 0x108)
print('Name:', name)

# Check queue at +0xe4
q_id = rp32(earth_planet + 0xe4)
print('queue at +0xe4:', q_id)

# Check queue at +0x880 if any
print('val at +0x880:', rp32(earth_planet + 0x880))

# Check colony ref at +0xc0, +0x4b0, etc.
for off in [0x0, 0x8, 0x10, 0x20, 0x98, 0xc0, 0xc4, 0x4b0, 0x880]:
    print(f' +0x{off:03x}: 32bit={rp32(earth_planet + off)} (0x{rp32(earth_planet + off):x}), 64bit=0x{rp64(earth_planet + off):x}')
