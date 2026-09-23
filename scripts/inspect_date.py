import ctypes
from ctypes import wintypes
import struct

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400

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

def read_bytes(h_proc, addr, size):
    buf = ctypes.create_string_buffer(size)
    bytes_read = ctypes.c_size_t(0)
    ok = ReadProcessMemory(h_proc, addr, buf, size, ctypes.byref(bytes_read))
    if not ok:
        return None
    return buf.raw[:bytes_read.value]

def read_u64(h_proc, addr):
    data = read_bytes(h_proc, addr, 8)
    return struct.unpack("<Q", data)[0] if data else None

def read_u32(h_proc, addr):
    data = read_bytes(h_proc, addr, 4)
    return struct.unpack("<I", data)[0] if data else None

def main():
    pid = 92116
    h_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    base = 0x7FF75ED50000

    idler_ptr = read_u64(h_proc, base + 0x3287900)
    print(f"InGameIdler: 0x{idler_ptr:X}")

    for offset in range(0x570, 0x5E0, 4):
        val = read_u32(h_proc, idler_ptr + offset)
        print(f"  +0x{offset:03X}: {val} (0x{val:08X})")

    CloseHandle(h_proc)

if __name__ == "__main__":
    main()
