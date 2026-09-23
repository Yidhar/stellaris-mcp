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

ft_mgr = r64(base + 0x3113038)
ft_arr = r64(ft_mgr + 0x18)

for tid in range(4):
    p = r64(ft_arr + tid * 16 + 8)
    if p:
        d_arr = r64(p + 0x28)
        d_cnt = r32(p + 0x34)
        print(f"\nTemplate {tid} (at {hex(p)}): designs_count={d_cnt}")
        for d in range(d_cnt):
            entry = d_arr + d * 0x560
            did = r32(entry + 0x28)
            cnt108 = r32(entry + 0x108)
            cnt4b0 = r32(entry + 0x4b0)
            quota558 = r32(entry + 0x558)
            print(f"  Design {d}: id={did}, +0x108={cnt108}, +0x4b0={cnt4b0}, quota={quota558}")
