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

earth = 0x2B4633F38D0

# Let's inspect all pointers inside earth (0 to 0x500)
for off in range(0, 0x500, 8):
    p = rp(earth + off)
    if p > 0x10000 and p < 0x7FFFFFFFFFFF:
        # read string at p, p+8, p+16, p+20
        buf = (ctypes.c_char * 64)()
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(p), buf, 64, None)
        raw = bytes(buf)
        if b'\x00' in raw:
            s = raw.split(b'\x00')[0]
            if len(s) > 2 and all(32 <= b < 127 for b in s):
                print(f"earth + 0x{off:X} -> '{s.decode()}'")
        # Also check p+0x10, p+0x20
        for sub_off in [16, 24, 32]:
            buf2 = (ctypes.c_char * 64)()
            kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(p + sub_off), buf2, 64, None)
            raw2 = bytes(buf2)
            if b'\x00' in raw2:
                s2 = raw2.split(b'\x00')[0]
                if len(s2) > 2 and all(32 <= b < 127 for b in s2):
                    print(f"earth + 0x{off:X} (+0x{sub_off:X}) -> '{s2.decode()}'")

