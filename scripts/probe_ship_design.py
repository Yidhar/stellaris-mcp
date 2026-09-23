import ctypes, struct
import inject, reload_dll
import sys
sys.stdout.reconfigure(encoding='utf-8')

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

p18 = 0x270215e4720
print("p18:", hex(p18))
print("p18 vtable:", hex(r64(p18)))
print("p18 + 0x20 string:", repr(rstr(p18 + 0x20)))
for off in range(0, 0x80, 8):
    s = rstr(p18 + off)
    if s:
        print(f"  p18 string at +{hex(off)}: {repr(s)}")
