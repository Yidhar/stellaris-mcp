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
    if not h_proc:
        print("Failed to open process:", ctypes.get_last_error())
        return

    base = 0x7FF75ED50000
    idler_ptr_addr = base + 0x3287900
    idler_ptr = read_u64(h_proc, idler_ptr_addr)
    print(f"Base: 0x{base:X}")
    print(f"g_InGameIdler pointer address: 0x{idler_ptr_addr:X} -> 0x{idler_ptr:X}" if idler_ptr else "None")

    if idler_ptr:
        print(f"\n--- InGameIdler at 0x{idler_ptr:X} ---")
        # Dump offsets around 0x160 - 0x1A0
        data = read_bytes(h_proc, idler_ptr + 0x160, 0x40)
        if data:
            for offset in range(0x160, 0x1A0, 8):
                val = read_u64(h_proc, idler_ptr + offset)
                val_u32_low = read_u32(h_proc, idler_ptr + offset)
                val_u32_high = read_u32(h_proc, idler_ptr + offset + 4)
                print(f"  +0x{offset:03X}: 0x{val:016X} (low32={val_u32_low}, high32={val_u32_high})")

    CloseHandle(h_proc)

if __name__ == "__main__":
    main()
