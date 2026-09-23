import ctypes, struct
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

def rstr(addr):
    buf = ctypes.create_string_buffer(64)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 64, ctypes.byref(read)):
        s = buf.raw[:read.value]
        null_idx = s.find(b'\0')
        if null_idx != -1: s = s[:null_idx]
        return s.decode('ascii', errors='replace')
    return ""

# There is a token string table:
# Let's find string table for token 0x2C56, 0x3B40, 0x3B39
# Often there is an array of const char* at some table
# Let's search binary for "fleet" or check 0x7ff779784350
print("Done")
