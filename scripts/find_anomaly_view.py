import ctypes
from ctypes import wintypes
import struct
import capstone

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

def read_bytes(h_proc, addr, size):
    buf = ctypes.create_string_buffer(size)
    bytes_read = ctypes.c_size_t(0)
    ok = ReadProcessMemory(h_proc, addr, buf, size, ctypes.byref(bytes_read))
    if not ok:
        return None
    return buf.raw[:bytes_read.value]

def main():
    pid = 105100
    h_proc = OpenProcess(PROCESS_VM_READ, False, pid)
    base = 0x7FF75ED50000

    target = b"anomaly_view_window"
    print("Searching for 'anomaly_view_window' string...")
    matches = []
    chunk = 0x200000
    for rva in range(0x1000, 0x3500000, chunk):
        data = read_bytes(h_proc, base + rva, chunk + len(target))
        if not data:
            continue
        idx = 0
        while True:
            idx = data.find(target, idx)
            if idx == -1:
                break
            addr = base + rva + idx
            matches.append(addr)
            print(f"Found string at: 0x{addr:X} (RVA 0x{addr - base:X})")
            idx += len(target)

    # Search for references
    for str_addr in matches:
        print(f"\nSearching for references to string 0x{str_addr:X}...")
        for rva in range(0x1000, 0x2200000, chunk):
            data = read_bytes(h_proc, base + rva, chunk + 16)
            if not data:
                continue
            for i in range(len(data) - 7):
                disp = struct.unpack("<i", data[i+3:i+7])[0]
                ins_addr = base + rva + i
                if ins_addr + 7 + disp == str_addr:
                    print(f"  Ref at 0x{ins_addr:X} (RVA 0x{ins_addr - base:X})")

    CloseHandle(h_proc)

if __name__ == "__main__":
    main()
