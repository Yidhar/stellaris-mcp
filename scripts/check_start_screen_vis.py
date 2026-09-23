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

def read_u8(h, addr):
    buf = ctypes.create_string_buffer(1)
    read = ctypes.c_size_t()
    ok = ReadProcessMemory(h, addr, buf, 1, ctypes.byref(read))
    return struct.unpack('<B', buf.raw)[0] if ok else None

def main():
    h = OpenProcess(0x10, False, 92116)
    base = 0x7FF75ED50000

    idler = read_u64(h, base + 0x3287900)
    start_screen = read_u64(h, idler + 0xBE8)
    print(f"start_screen: 0x{start_screen:X}")
    if start_screen:
        ui_window = read_u64(h, start_screen + 0x78)
        print(f"ui_window: 0x{ui_window:X}")
        if ui_window:
            is_visible = read_u8(h, ui_window + 0x41)
            print(f"ui_window is_visible (+0x41): {is_visible}")
        is_active = read_u8(h, start_screen + 0x258)
        print(f"start_screen is_active (+0x258): {is_active}")

if __name__ == "__main__":
    main()
