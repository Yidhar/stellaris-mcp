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

def read_u32(h, addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    ok = ReadProcessMemory(h, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read))
    return struct.unpack('<I', buf.raw)[0] if ok else 0

def main():
    pid = 88688
    h = OpenProcess(PROCESS_VM_READ, False, pid)
    base = 0x7FF75ED50000

    idler = read_u64(h, base + 0x3287900)
    alert_win = read_u64(h, idler + 0xBC8)
    print(f"alert_win: 0x{alert_win:X}")

    for off in range(0x340, 0x378, 8):
        print(f"  +0x{off:03X}: 0x{read_u64(h, alert_win + off):016X}")

    # Check how many entries exist with non-null ui_win
    non_null_count = 0
    vis_count = 0
    for i in range(128):
        ui_win = read_u64(h, alert_win + 0x368 + i * 0x110)
        if ui_win != 0:
            non_null_count += 1
            vis_buf = ctypes.create_string_buffer(1)
            read = ctypes.c_size_t()
            ReadProcessMemory(h, ctypes.c_void_p(ui_win + 0x41), vis_buf, 1, ctypes.byref(read))
            vis = struct.unpack('<B', vis_buf.raw)[0]
            if vis == 1:
                vis_count += 1
                print(f"  Alert {i}: ui_win=0x{ui_win:X}, vis={vis}")

    print(f"Total non-null UI windows in array: {non_null_count}, visible alerts: {vis_count}")
    CloseHandle(h)

if __name__ == "__main__":
    main()
