import ctypes, struct
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def rstr(addr):
    buf = ctypes.create_string_buffer(64)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 64, ctypes.byref(read)):
        s = buf.raw[:read.value]
        null_idx = s.find(b'\0')
        if null_idx != -1: s = s[:null_idx]
        return s
    return b""

print("p18:", rstr(0x270072fa790))
print("p40:", rstr(0x2701bb27670))
