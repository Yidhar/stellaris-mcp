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

for off in range(0x3112F00, 0x3113100, 8):
    mgr = r64(base + off)
    if mgr > 0x10000 and mgr < 0x7FFFFFFFFFFF:
        arr = r64(mgr + 0x18)
        cap = r32(mgr + 0x20)
        if cap in (512, 1024, 2048, 4096, 8192, 16384, 32768, 65536) and arr > 0x10000:
            # count valid items
            cnt = 0
            sample_ids = []
            for i in range(cap):
                p = r64(arr + i * 16 + 8)
                if p:
                    cnt += 1
                    if len(sample_ids) < 5:
                        sample_ids.append((r32(p + 8), hex(p)))
            print(f"Global at base+{hex(off)}: mgr={hex(mgr)}, cap={cap}, items={cnt}, samples={sample_ids}")
