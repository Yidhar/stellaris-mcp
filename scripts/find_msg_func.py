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

    # Read from 0x323300 to 0x323500
    raw = read_bytes(h, base + 0x323200, 0x300)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

    # Look for function prologues (sub rsp / push rbp)
    target = base + 0x3234EB
    for offset in range(0, 0x200):
        addr = base + 0x323200 + offset
        insns = list(md.disasm(raw[offset:], addr))
        addrs = [ins.address for ins in insns]
        if target in addrs:
            print(f"Synchronized stream starting at 0x{addr:X}:")
            # Find the first instruction of the function
            for ins in insns:
                if ins.mnemonic in ['push', 'sub'] and 'rsp' in ins.op_str:
                    print(f"Possible start: 0x{ins.address:X} ({ins.mnemonic} {ins.op_str})")
                if ins.address >= target - 0x30 and ins.address <= target + 0x30:
                    print(f"  0x{ins.address:X}: {ins.mnemonic} {ins.op_str}")
            break

    CloseHandle(h)

if __name__ == "__main__":
    main()
