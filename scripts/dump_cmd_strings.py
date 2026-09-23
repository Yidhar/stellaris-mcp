import ctypes
import inject, reload_dll
import sys
sys.stdout.reconfigure(encoding='utf-8')

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(read)):
        return buf.raw[:read.value]
    return b""

addr = base + 0x230E213
chunk = read_bytes(addr - 100, 300)
strings = chunk.split(b'\0')
for s in strings:
    if s:
        print(" ", s)
