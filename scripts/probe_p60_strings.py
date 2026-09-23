import ctypes, struct
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_str(addr):
    buf = ctypes.create_string_buffer(64)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 64, ctypes.byref(read)):
        s = buf.raw[:read.value]
        null_idx = s.find(b'\0')
        if null_idx != -1: s = s[:null_idx]
        return s
    return b""

print("0x26ffd8fdda0:", repr(read_str(0x26ffd8fdda0)))
print("0x26ffd8fde30:", repr(read_str(0x26ffd8fde30)))
