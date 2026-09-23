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
    size = r64(addr + 16)
    cap = r64(addr + 24)
    if size == 0 or size > 100: return ""
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    ptr = addr if cap < 16 else r64(addr)
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(ptr), buf, size, ctypes.byref(read)):
        return buf.raw[:read.value].decode('utf-8', errors='replace')
    return ""

p = 0x270603d8010
print(f"Object at {hex(p)}:")
print(f"  vtable: {hex(r64(p))} (rel={hex(r64(p)-base)})")
for off in range(0, 0x80, 8):
    print(f"  +{hex(off)}: u64={hex(r64(p+off))} | u32=({r32(p+off)}, {r32(p+off+4)})")

# Check all string offsets in p
for off in range(0, 0x150, 8):
    s = rstr(p + off)
    if s:
        print(f"  String at +{hex(off)}: {repr(s)}")
