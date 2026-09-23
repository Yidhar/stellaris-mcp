import capstone
import ctypes
from ctypes import wintypes

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
OpenProcess = kernel32.OpenProcess
OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
OpenProcess.restype = wintypes.HANDLE

ReadProcessMemory = kernel32.ReadProcessMemory
ReadProcessMemory.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.LPVOID, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
ReadProcessMemory.restype = wintypes.BOOL

def read_bytes(h_proc, addr, size):
    buf = ctypes.create_string_buffer(size)
    bytes_read = ctypes.c_size_t(0)
    ok = ReadProcessMemory(h_proc, addr, buf, size, ctypes.byref(bytes_read))
    if not ok:
        return None
    return buf.raw[:bytes_read.value]

def disasm_func(h_proc, addr, size=0x100):
    code = read_bytes(h_proc, addr, size)
    if not code:
        print(f"Failed to read at 0x{addr:X}")
        return
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for ins in md.disasm(code, addr):
        print(f"  0x{ins.address:X}:  {ins.mnemonic:<8} {ins.op_str}")

def main():
    pid = 92116
    h_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    base = 0x7FF75ED50000

    print("--- AddEventWindow (0x3393a0) ---")
    disasm_func(h_proc, base + 0x3393a0, 0x80)

    print("\n--- RemoveEventWindow (0x3394d0) ---")
    disasm_func(h_proc, base + 0x3394d0, 0x80)

if __name__ == "__main__":
    main()
