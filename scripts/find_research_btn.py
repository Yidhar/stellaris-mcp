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
    return buf.raw[:bytes_read.value] if ok else None

def main():
    pid = 105100
    h_proc = OpenProcess(PROCESS_VM_READ, False, pid)
    base = 0x7FF75ED50000

    start_addr = base + 0x1057E00
    size = 0x2000
    raw = read_bytes(h_proc, start_addr, size)

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

    # Disassemble and search for string references or calls
    print(f"Scanning CAnomalyView methods from 0x{start_addr:X} to 0x{start_addr + size:X}...")
    for ins in md.disasm(raw, start_addr):
        if ins.mnemonic == 'call':
            # print calls
            pass
        if 'rip +' in ins.op_str or 'rip -' in ins.op_str:
            # Check displacement
            pass

    # Search for "research" string in binary
    target = b"research\x00"
    matches = []
    chunk = 0x200000
    for rva in range(0x1000, 0x3000000, chunk):
        data = read_bytes(h_proc, base + rva, chunk + len(target))
        if not data: continue
        idx = 0
        while True:
            idx = data.find(target, idx)
            if idx == -1: break
            matches.append(base + rva + idx)
            idx += len(target)

    print(f"Found {len(matches)} occurrences of 'research'. Checking refs in CAnomalyView range...")
    for str_addr in matches:
        for i in range(len(raw) - 7):
            disp = struct.unpack("<i", raw[i+3:i+7])[0]
            ins_addr = start_addr + i
            if ins_addr + 7 + disp == str_addr:
                print(f"  Ref to 'research' at 0x{ins_addr:X} (RVA 0x{ins_addr - base:X})")

    CloseHandle(h_proc)

if __name__ == "__main__":
    main()
