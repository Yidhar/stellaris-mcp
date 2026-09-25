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

cmgr = rp(base + 0x3112F50)
carr = rp(cmgr + 0x18)
player = rp(carr + 8)

for off in range(0x1DF8, 0x2100, 8):
    p = rp(player + off)
    # Check if p points to array of size >= 26
    # Let's inspect
    if p > 0x10000 and p < 0x7FFFFFFFFFFF:
        buf = (ctypes.c_int64 * 10)()
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(p), buf, 80, None)
        vals = [f"{b/100000.0:.2f}" for b in buf[:5]]
        print(f"+0x{off:X}: 0x{p:X} -> {vals}, trade={buf[8]/100000.0:.2f}")
    else:
        if p != 0 and p < 0x1000:
            print(f"+0x{off:X}: {p}")
