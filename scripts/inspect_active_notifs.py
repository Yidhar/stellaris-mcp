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

def read_bytes(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    ok = ReadProcessMemory(h, addr, buf, size, ctypes.byref(read))
    return buf.raw[:read.value] if ok else None

def main():
    pid = 105100
    h = OpenProcess(0x10, False, pid)
    base = 0x7FF75ED50000

    rdi = read_u64(h, base + 0x3287360)
    arr_ptr = read_u64(h, rdi + 0x498)
    count = read_u32(h, rdi + 0x4a4)
    print(f"Active Notifications count: {count}, array: 0x{arr_ptr:X}")

    for i in range(count):
        elem = read_u64(h, arr_ptr + i * 8)
        print(f"\nNotification #{i+1} at 0x{elem:X}:")
        vtable = read_u64(h, elem)
        print(f"  vtable: 0x{vtable:X} (RVA 0x{vtable - base:X})")
        for off in range(0, 0x80, 8):
            val = read_u64(h, elem + off)
            print(f"    +0x{off:02X}: 0x{val:016X}")

    CloseHandle(h)

if __name__ == "__main__":
    main()
