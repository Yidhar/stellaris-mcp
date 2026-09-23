import capstone
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

def read_bytes(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    ok = ReadProcessMemory(h, addr, buf, size, ctypes.byref(read))
    return buf.raw[:read.value] if ok else None

def disasm(h, addr, size=0x60):
    code = read_bytes(h, addr, size)
    if not code: return
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for ins in md.disasm(code, addr):
        print(f"    0x{ins.address:X}:  {ins.mnemonic:<8} {ins.op_str}")

def main():
    h = OpenProcess(0x10, False, 92116)
    base = 0x7FF75ED50000

    vt_addr = base + 0x25C9030 # 0x7FF761319030
    print(f"Reading vtable at 0x{vt_addr:X}...")
    for idx in range(30):
        fn = read_u64(h, vt_addr + idx * 8)
        print(f"  vtable[{idx:2d}]: 0x{fn:X} (RVA 0x{fn - base:X})")

    # Let's inspect the first few methods
    print("\n--- vtable[0] (Destructor / Dismiss) ---")
    disasm(h, read_u64(h, vt_addr))

if __name__ == "__main__":
    main()
