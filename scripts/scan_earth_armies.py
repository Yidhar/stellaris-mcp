import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

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
    if sz == 0 or sz > 512: return ""
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = rp(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

earth = 0x2B4633F38D0

# In earth CColony, scan for army vectors or strings
for off in range(0, 0x500, 8):
    v_ptr = rp(earth + off + 8)
    v_cnt = ru32(earth + off + 0x14)
    if v_ptr > 0x10000 and v_ptr < 0x7FFFFFFFFFFF and 1 <= v_cnt <= 20:
        print(f"earth + 0x{off:X}: cnt={v_cnt}, ptr=0x{v_ptr:X}")
        # check elements
        for i in range(v_cnt):
            e = rp(v_ptr + i * 8)
            u = ru32(v_ptr + i * 4)
            print(f"   [{i}]: ptr=0x{e:X}, u32={u}")

