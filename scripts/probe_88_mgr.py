import ctypes, struct
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

target_addr = 0x7FF7789E5C61 + 0x22FB097
print(f"Target Addr: {hex(target_addr)} (rel=base+{hex(target_addr - base)})")
mgr_ptr = r64(target_addr)
print(f"Manager Ptr: {hex(mgr_ptr)}")
if mgr_ptr:
    for off in range(0, 0x40, 8):
        print(f"  +{hex(off)}: {hex(r64(mgr_ptr + off))}")
