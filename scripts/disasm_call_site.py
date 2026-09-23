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

def main():
    h_proc = OpenProcess(0x10, False, 92116)
    base = 0x7FF75ED50000

    # Read from 0x9913B0
    size = 0x100
    buf = ctypes.create_string_buffer(size)
    bytes_read = ctypes.c_size_t(0)
    ReadProcessMemory(h_proc, base + 0x9913A0, buf, size, ctypes.byref(bytes_read))

    raw = buf.raw
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

    # Search for an offset where an instruction lands exactly at 0x7FF75F6E13DD
    target_addr = base + 0x9913DD
    for offset in range(0x40):
        addr = base + 0x9913A0 + offset
        insns = list(md.disasm(raw[offset:], addr))
        addrs = [ins.address for ins in insns]
        if target_addr in addrs:
            print(f"Found synchronized alignment at offset {offset}:")
            for ins in insns:
                if ins.address >= target_addr - 0x20 and ins.address <= target_addr + 0x60:
                    print(f"  0x{ins.address:X}:  {ins.mnemonic:<8} {ins.op_str}")
            break

if __name__ == "__main__":
    main()
