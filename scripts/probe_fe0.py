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

mgr = r64(base + 0x3112FE0)
arr = r64(mgr + 0x18)
cap = r32(mgr + 0x20)
print(f"Manager 0x3112FE0: arr={hex(arr)}, cap={cap}")

p0 = r64(arr + 8)
print(f"Item 0 at {hex(p0)}:")
print(f"  vtable: {hex(r64(p0))} (rel={hex(r64(p0)-base)})")
for off in range(0, 0x80, 8):
    print(f"  +{hex(off)}: u64={hex(r64(p0+off))} | u32=({r32(p0+off)}, {r32(p0+off+4)})")
