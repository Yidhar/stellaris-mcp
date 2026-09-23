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

def read_u64(h, addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    ok = ReadProcessMemory(h, addr, buf, 8, ctypes.byref(read))
    return struct.unpack('<Q', buf.raw)[0] if ok else None

def read_u32(h, addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    ok = ReadProcessMemory(h, addr, buf, 4, ctypes.byref(read))
    return struct.unpack('<I', buf.raw)[0] if ok else None

def main():
    pid = 105100
    h = OpenProcess(0x10, False, pid)
    base = 0x7FF75ED50000

    target_ptr_addr = base + 0x3287360
    rdi = read_u64(h, target_ptr_addr)
    print(f"Pointer at 0x{target_ptr_addr:X}: 0x{rdi:X}" if rdi else "None")

    if rdi:
        count_4a4 = read_u32(h, rdi + 0x4a4)
        print(f"Count at [rdi + 0x4a4]: {count_4a4}")
        for off in range(0x480, 0x4D0, 8):
            val = read_u64(h, rdi + off)
            print(f"  +0x{off:03X}: 0x{val:016X}")

    CloseHandle(h)

if __name__ == "__main__":
    main()
