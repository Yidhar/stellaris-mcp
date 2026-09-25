import sys, ctypes
sys.path.append(r'D:\stellarismcp\scripts')
import reload_dll, inject

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

pmgr = rp(base + 0x3113148)
parr = rp(pmgr + 0x18)

p11 = rp(parr + 11 * 16 + 8) # Earth (Player, Country 0)
p63 = rp(parr + 63 * 16 + 8) # Khor-I (Player, Country 0)
p3  = rp(parr + 3 * 16 + 8)  # Deneb (AI Empire)
p6  = rp(parr + 6 * 16 + 8)  # Vivisandia (AI Empire)

print(f"p11: 0x{p11:X}, p63: 0x{p63:X}, p3: 0x{p3:X}, p6: 0x{p6:X}")

for off in range(0, 0x800, 4):
    v11 = ru32(p11 + off)
    v63 = ru32(p63 + off)
    v3 = ru32(p3 + off)
    v6 = ru32(p6 + off)
    # Owner of 11 and 63 is 0, owner of 3 and 6 is > 0
    if v11 == 0 and v63 == 0 and v3 > 0 and v6 > 0 and v3 < 50 and v6 < 50:
        print(f"Candidate owner offset +0x{off:03X}: p11={v11}, p63={v63}, p3={v3}, p6={v6}")
