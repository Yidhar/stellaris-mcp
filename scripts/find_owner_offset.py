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

colony_mgr = rp(base + 0x3113148)
colony_arr = rp(colony_mgr + 0x18)

obj11 = rp(colony_arr + 11 * 16 + 8) # Earth (Player owned)
obj63 = rp(colony_arr + 63 * 16 + 8) # Khor-I (Player owned)
obj12 = rp(colony_arr + 12 * 16 + 8) # Diarmu (NOT player owned)
obj31 = rp(colony_arr + 31 * 16 + 8) # Iolam (NOT player owned)

print(f"obj11: 0x{obj11:X}, obj63: 0x{obj63:X}, obj12: 0x{obj12:X}")

# Let's find all offsets where obj11 and obj63 have the SAME value, but obj12 has a DIFFERENT value!
matching_offsets = []
for off in range(0, 0x600, 4):
    v11 = ru32(obj11 + off)
    v63 = ru32(obj63 + off)
    v12 = ru32(obj12 + off)
    v31 = ru32(obj31 + off)
    
    # If 11 and 63 share a country ID or owner flag that is different from 12 and 31
    if v11 == v63 and v11 != v12:
        print(f"Offset +0x{off:03X}: player(11,63)={v11} != other(12)={v12}, other(31)={v31}")
