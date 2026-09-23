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

# Check country manager
c_mgr = r64(base + 0x3112F50)
c_arr = r64(c_mgr + 0x18)
p_country = r64(c_arr + 8)

fleet_mgr = r64(base + 0x3113008)
fleet_arr = r64(fleet_mgr + 0x18)

vec_ptr = r64(p_country + 0x3ab8 + 8)
vec_cnt = r32(p_country + 0x3ab8 + 20)
print(f"country+0x3ab8: ptr={hex(vec_ptr)}, cnt={vec_cnt}")

for i in range(vec_cnt):
    fid = r32(vec_ptr + i * 4)
    p = r64(fleet_arr + (fid & 0xFFFFFF) * 16 + 8)
    print(f"[{i}] Fleet ID: {fid}, Ptr: {hex(p)}")
    if p:
        # Check vtable
        vt = r64(p)
        print(f"    VT: {hex(vt)} (rel={hex(vt-base)})")
        print(f"    Power (+0x100): {r32(p + 0x100) / 1000.0}")
