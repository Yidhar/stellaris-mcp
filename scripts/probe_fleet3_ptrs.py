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

# Check where CFleet stores ships
# In Fleet 3 at 0x270605e6500:
# Let's search all pointers inside Fleet 3
f3 = 0x270605e6500
for off in range(0, 0x190, 8):
    p = r64(f3 + off)
    if p > 0x10000 and p < 0x7FFFFFFFFFFF:
        # Check what p points to
        sub_p = r64(p)
        sub_u32 = r32(p)
        print(f"+{hex(off)}: p={hex(p)}, *p={hex(sub_p)}, u32={sub_u32}")
