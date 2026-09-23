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
    target_addr = base + 0x32878F0

    # Scan .text section (usually 0x1000 to ~0x2500000)
    chunk_size = 0x100000
    print(f"Scanning for references to 0x{target_addr:X}...")

    matches = []
    for rva in range(0x1000, 0x2200000, chunk_size):
        data = read_bytes(h_proc, base + rva, chunk_size + 16)
        if not data:
            continue
        for i in range(len(data) - 7):
            # Check for 4-byte displacement
            disp = struct.unpack("<i", data[i+3:i+7])[0]
            ins_addr = base + rva + i
            next_ins = ins_addr + 7
            if next_ins + disp == target_addr:
                matches.append((ins_addr, data[i:i+7]))

    print(f"Found {len(matches)} reference(s):")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for addr, raw in matches[:20]:
        insns = list(md.disasm(raw, addr))
        ins_str = f"{insns[0].mnemonic} {insns[0].op_str}" if insns else raw.hex()
        print(f"  0x{addr:X} (RVA 0x{addr - base:X}): {ins_str}")

    CloseHandle(h_proc)

if __name__ == "__main__":
    main()
