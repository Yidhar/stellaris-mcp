import capstone
import ctypes
from ctypes import wintypes

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

def main():
    pid = 105100
    h = OpenProcess(0x10, False, pid)
    base = 0x7FF75ED50000

    raw = read_bytes(h, base + 0x322F00, 0x600)
    target = base + 0x3234EB
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

    # Walk from the beginning looking for ret followed by a new function
    insns = list(md.disasm(raw, base + 0x322F00))
    func_starts = []
    for i, ins in enumerate(insns):
        if ins.mnemonic == 'ret':
            # Next instruction could be a function start
            if i + 1 < len(insns):
                func_starts.append(insns[i+1].address)

    print(f"Function starts found before target:")
    for fs in func_starts:
        if fs <= target:
            print(f"  0x{fs:X} (RVA 0x{fs - base:X})")

    CloseHandle(h)

if __name__ == "__main__":
    main()
