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

mgr = r64(base + 0x3110CF8)
arr = r64(mgr + 0x18)
p3 = r64(arr + 3 * 16 + 8)
print(f"Manager 0x3110CF8 item 3: {hex(p3)}")
if p3:
    print(f"  vtable: {hex(r64(p3))}")
    print(f"  id at +0x30: {r32(p3 + 0x30)}")
    print(f"  id at +0x08: {r32(p3 + 0x08)}")
    for off in range(0, 0x100, 8):
        print(f"  +{hex(off)}: u64={hex(r64(p3 + off))} | u32=({r32(p3 + off)}, {r32(p3 + off + 4)})")
