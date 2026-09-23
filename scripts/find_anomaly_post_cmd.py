import capstone
import ctypes
from ctypes import wintypes
import struct

PROCESS_VM_READ = 0x0010
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
OpenProcess = kernel32.OpenProcess
OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
OpenProcess.restype = wintypes.HANDLE

ReadProcessMemory = kernel32.ReadProcessMemory
ReadProcessMemory.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.LPVOID, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
ReadProcessMemory.restype = wintypes.BOOL

CloseHandle = kernel32.CloseHandle
CloseHandle.argtypes = [wintypes.HANDLE]
CloseHandle.restype = wintypes.BOOL

def read_bytes(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    ok = ReadProcessMemory(h, addr, buf, size, ctypes.byref(read))
    return buf.raw[:read.value] if ok else None

def main():
    pid = 105100
    h = OpenProcess(0x10, False, pid)
    base = 0x7FF75ED50000

    # PostCommand is at RVA 0x648970 (0x7FF75F398970)
    post_cmd = base + 0x648970

    start_addr = base + 0x1050000
    size = 0x10000
    raw = read_bytes(h, start_addr, size)

    print(f"Searching for calls to PostCommand (0x{post_cmd:X}) in 0x1050000-0x1060000...")
    for i in range(len(raw) - 5):
        if raw[i] == 0xE8:
            disp = struct.unpack("<i", raw[i+1:i+5])[0]
            call_site = start_addr + i
            if call_site + 5 + disp == post_cmd:
                print(f"  PostCommand call at 0x{call_site:X} (RVA 0x{call_site - base:X})")

    CloseHandle(h)

if __name__ == "__main__":
    main()
