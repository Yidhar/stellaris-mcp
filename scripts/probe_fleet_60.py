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

fleet_mgr = r64(base + 0x3113008)
fleet_arr = r64(fleet_mgr + 0x18)

for fid in [0, 1, 2, 3, 4]:
    p = r64(fleet_arr + fid * 16 + 8)
    if p:
        p60 = r64(p + 0x60)
        vt60 = r64(p60) if p60 else 0
        print(f"Fleet {fid}: p60={hex(p60)}, vt60={hex(vt60)} (rel={hex(vt60-base) if vt60 else 0})")
        if p60:
            for off in range(0, 0x80, 8):
                print(f"   +{hex(off)}: u64={hex(r64(p60 + off))} | u32=({r32(p60 + off)}, {r32(p60 + off + 4)})")
