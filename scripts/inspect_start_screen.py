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
    h = OpenProcess(0x10, False, 92116)
    base = 0x7FF75ED50000

    idler = read_u64(h, base + 0x3287900)
    print(f"idler: 0x{idler:X}")
    start_screen = read_u64(h, idler + 0xBE8)
    print(f"start_screen at [idler + 0xBE8]: 0x{start_screen:X}")
    if start_screen:
        vtable = read_u64(h, start_screen)
        print(f"start_screen vtable: 0x{vtable:X} (RVA 0x{vtable - base:X})")
        # Check ui_window or fields in start_screen
        for off in range(0, 0x100, 8):
            val = read_u64(h, start_screen + off)
            print(f"  +0x{off:02X}: 0x{val:016X}")

if __name__ == "__main__":
    main()
