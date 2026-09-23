import ctypes
from ctypes import wintypes
import struct

PROCESS_VM_READ = 0x0010
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
OpenProcess = kernel32.OpenProcess
OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
OpenProcess.restype = wintypes.HANDLE

ReadProcessMemory = kernel32.ReadProcessMemory
ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
ReadProcessMemory.restype = wintypes.BOOL

CloseHandle = kernel32.CloseHandle
CloseHandle.argtypes = [wintypes.HANDLE]
CloseHandle.restype = wintypes.BOOL

def read_u64(h, addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    ok = ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read))
    return struct.unpack('<Q', buf.raw)[0] if ok else 0

def main():
    pid = 88688
    h = OpenProcess(PROCESS_VM_READ, False, pid)
    base = 0x7FF75ED50000

    idler = read_u64(h, base + 0x3287900)
    alert_win = read_u64(h, idler + 0xBC8)
    vt = read_u64(h, alert_win)
    print(f"alert_win: 0x{alert_win:X}, vtable: 0x{vt:X} (RVA 0x{vt - base:X})")

    # Let's inspect where OnAlertClick is referenced
    # RVA 0xA46EC0
    # Let's check functions referencing 0xA46EC0 or 0xBC8
    CloseHandle(h)

if __name__ == "__main__":
    main()
