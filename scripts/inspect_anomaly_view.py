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
    h = OpenProcess(0x10, False, 105100)
    base = 0x7FF75ED50000

    idler = read_u64(h, base + 0x3287900)
    print(f"idler: 0x{idler:X}")
    anomaly_view = read_u64(h, idler + 0xB08)
    print(f"anomaly_view at [idler + 0xB08]: 0x{anomaly_view:X}")
    if anomaly_view:
        vtable = read_u64(h, anomaly_view)
        print(f"anomaly_view vtable: 0x{vtable:X} (RVA 0x{vtable - base:X})")
        ui_win = read_u64(h, anomaly_view + 0x78)
        print(f"ui_window (+0x78): 0x{ui_win:X}")
        if ui_win:
            vis = read_u8(h, ui_win + 0x41)
            print(f"ui_window is_visible (+0x41): {vis}")

if __name__ == "__main__":
    main()
