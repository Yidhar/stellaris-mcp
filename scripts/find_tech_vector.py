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

cmgr = read_ptr(base + 0x3112F50)
carr = read_ptr(cmgr + 0x18)
player = read_ptr(carr + 8)

tech_mgr = player + 0x16E0
# Researched techs in CTechnology:
# In tech_manager.cpp, let's see where researched techs are stored!
# Let's inspect pointers inside tech_mgr
for off in range(0, 0x100, 8):
    p = read_ptr(tech_mgr + off)
    cnt = read_u32(tech_mgr + off + 8)
    if p > 0x10000000000 and 0 < cnt < 10000:
        print(f"Tech vector at +0x{off:X}: ptr=0x{p:X}, cnt={cnt}")
