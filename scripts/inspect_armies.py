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

mgr = rp(base + 0x3112FC8)
arr = rp(mgr + 0x18)
cap = ru32(mgr + 0x20)

print(f"ArmyManager: 0x{mgr:X}, cap={cap}")
for i in range(min(cap, 100)):
    p = rp(arr + i * 16 + 8)
    if not p: continue
    aid = ru32(p + 8)
    # Check owner country at p + 0x...
    # Check army type
    strings = []
    for off in range(0, 0x100, 8):
        s = read_pdx_string(p + off)
        if s and len(s) > 1 and not s.startswith(" "):
            strings.append(f"+0x{off:X}:'{s}'")
    print(f"Army {i}: id={aid}, obj=0x{p:X}, {', '.join(strings[:4])}")

