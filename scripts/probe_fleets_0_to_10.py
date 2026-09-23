import ctypes, struct
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read)):
        return struct.unpack('<I', buf.raw)[0]
    return 0

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

def rstr(addr):
    # PDX string
    size = r64(addr + 16)
    cap = r64(addr + 24)
    if size == 0 or size > 100: return ""
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    ptr = addr if cap < 16 else r64(addr)
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(ptr), buf, size, ctypes.byref(read)):
        return buf.raw[:read.value].decode('utf-8', errors='replace')
    return ""

mgr = r64(base + 0x3113008)
arr = r64(mgr + 0x18)

for fid in range(10):
    p = r64(arr + fid * 16 + 8)
    if p:
        u8 = r32(p + 8)
        name = rstr(p + 0x68) # let's check strings inside p
        # Check string at p + 0x68, p + 0xA8, p + 0xB0 etc
        s_68 = rstr(p + 0x68)
        s_a8 = rstr(p + 0xA8)
        pow_val = r32(p + 0x100) / 1000.0
        # Check country_id in fleet: where is country_id?
        # let's search for 0 in p
        print(f"Fleet {fid}: ptr={hex(p)}, id={u8}, pow={pow_val}, s_68='{s_68}', s_a8='{s_a8}'")
        # Let's inspect fields of fleet 0, 1, 2, 3
        fields = [r32(p + off) for off in range(0, 0x190, 4)]
        print(f"  first 16 u32: {fields[:16]}")
