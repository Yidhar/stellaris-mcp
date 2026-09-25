import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def read_ptr(addr):
    val = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 8, None)
    return val.value

def read_u32(addr):
    val = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 4, None)
    return val.value

def read_u8(addr):
    val = ctypes.c_uint8()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 1, None)
    return val.value

def read_i64(addr):
    val = ctypes.c_int64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 8, None)
    return val.value

def read_pdx_string(addr):
    cap = read_ptr(addr + 24)
    sz = read_ptr(addr + 16)
    if sz == 0 or sz > 512: return ""
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = read_ptr(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

gal_market_ptr = base + 0x3111D60
gal_market = read_ptr(gal_market_ptr)
print(f"gal_market_ptr: 0x{gal_market_ptr:X} -> 0x{gal_market:X}")

if gal_market:
    print(f"gal_market VT: 0x{read_ptr(gal_market):X}")
    m_cnt = read_u32(gal_market + 0x5C)
    m_arr = read_ptr(gal_market + 0x50)
    print(f"gal_market entries: {m_cnt}, arr: 0x{m_arr:X}")
    
    # Check what is inside gal_market
    for off in range(0, 0x100, 8):
        v = read_ptr(gal_market + off)
        print(f"  +0x{off:X}: 0x{v:X}")
