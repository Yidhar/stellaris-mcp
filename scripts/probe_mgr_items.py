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

mgr = r64(base + 0x3112F88)
arr = r64(mgr + 0x18)
cap = r32(mgr + 0x20)
print(f"Manager 0x3112F88: arr={hex(arr)}, cap={cap}")

items = []
for i in range(cap):
    p = r64(arr + i * 16 + 8)
    if p:
        id_val = r32(p + 8)
        vt = r64(p)
        items.append((i, id_val, p, vt))

print(f"Total valid items: {len(items)}")
for i, id_val, p, vt in items[:15]:
    print(f"  [{i}]: id={id_val}, ptr={hex(p)}, vt={hex(vt)} (rel={hex(vt-base) if vt > base else 'none'})")
