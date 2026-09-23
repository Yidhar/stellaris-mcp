import ctypes
from ctypes import wintypes
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8")

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

def read_pdx_string(h, addr):
    raw = read_bytes(h, addr, 32)
    if not raw: return ""
    size = struct.unpack('<Q', raw[16:24])[0]
    cap = struct.unpack('<Q', raw[24:32])[0]
    if size == 0: return ""
    if cap < 16:
        return raw[:min(size, 15)].decode('utf-8', errors='ignore')
    else:
        heap_ptr = struct.unpack('<Q', raw[:8])[0]
        if heap_ptr and 0x10000 < heap_ptr < 0x7FFFFFFFFFFF:
            data = read_bytes(h, heap_ptr, min(size, 2048))
            return data.decode('utf-8', errors='ignore') if data else ""
    return ""

def main():
    pid = 105100
    h = OpenProcess(0x10, False, pid)
    base = 0x7FF75ED50000

    idler = read_u64(h, base + 0x3287900)
    anomaly_view = read_u64(h, idler + 0xB08)
    print(f"idler: 0x{idler:X}")
    print(f"anomaly_view: 0x{anomaly_view:X}")

    ui_win = read_u64(h, anomaly_view + 0x78)
    print(f"ui_window: 0x{ui_win:X}")
    vis = read_u8(h, ui_win + 0x41)
    print(f"is_visible: {vis}")

    # In CAnomalyView:
    # Let's inspect strings inside anomaly_view or ui_window
    for off in range(0, 0x500, 8):
        s = read_pdx_string(h, anomaly_view + off)
        if s and len(s) > 1:
            print(f"  anomaly_view +0x{off:03X}: {repr(s)}")

    CloseHandle(h)

if __name__ == "__main__":
    main()
