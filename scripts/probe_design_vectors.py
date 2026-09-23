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

p100 = 0x2706647dd10 # Wait, let's read d_arr + 0x100 directly
ft_mgr = r64(base + 0x3113038)
ft_arr = r64(ft_mgr + 0x18)
ft0 = r64(ft_arr + 8)
d_arr = r64(ft0 + 0x28)

ptr100 = r64(d_arr + 0x100)
cnt100 = r32(d_arr + 0x108)
print(f"Vector +0x100: ptr={hex(ptr100)}, cnt={cnt100}")
if ptr100:
    for i in range(cnt100):
        print(f"  [{i}]: u32={r32(ptr100 + i * 4)}, u64={hex(r64(ptr100 + i * 8))}")

ptr4a8 = r64(d_arr + 0x4a8)
cnt4b0 = r32(d_arr + 0x4b0)
print(f"\nVector +0x4a8: ptr={hex(ptr4a8)}, cnt={cnt4b0}")
if ptr4a8:
    for i in range(cnt4b0):
        print(f"  [{i}]: u32={r32(ptr4a8 + i * 4)}, u64={hex(r64(ptr4a8 + i * 8))}")
