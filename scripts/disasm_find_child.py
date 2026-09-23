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

def read_bytes(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    ok = ReadProcessMemory(h, addr, buf, size, ctypes.byref(read))
    return buf.raw[:read.value] if ok else None

def disasm(h, addr, size=0x80):
    code = read_bytes(h, addr, size)
    if not code: return
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for ins in md.disasm(code, addr):
        print(f"    0x{ins.address:X}:  {ins.mnemonic:<8} {ins.op_str}")

def main():
    h = OpenProcess(0x10, False, 92116)
    base = 0x7FF75ED50000

    print("--- CContainerWindow::FindChild (0x1D7DD00) ---")
    disasm(h, base + 0x1D7DD00, 0x80)

if __name__ == "__main__":
    main()
