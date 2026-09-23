import capstone
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

def main():
    pid = 92116
    h_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    base = 0x7FF75ED50000
    target = base + 0x13B5C30

    print(f"Searching for calls to 0x{target:X}...")
    chunk = 0x200000
    for rva in range(0x1000, 0x2200000, chunk):
        data = read_bytes(h_proc, base + rva, chunk + 16)
        if not data:
            continue
        for i in range(len(data) - 5):
            if data[i] == 0xE8: # call rel32
                disp = struct.unpack("<i", data[i+1:i+5])[0]
                call_site = base + rva + i
                if call_site + 5 + disp == target:
                    print(f"  Call at 0x{call_site:X} (RVA 0x{call_site - base:X})")

    CloseHandle(h_proc)

if __name__ == "__main__":
    main()
