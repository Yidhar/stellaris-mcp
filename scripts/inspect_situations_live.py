import ctypes
from ctypes import wintypes
import struct

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
ReadProcessMemory = kernel32.ReadProcessMemory
ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
ReadProcessMemory.restype = wintypes.BOOL

def read_mem(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    ok = ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(read))
    return buf.raw if ok else None

def read_u64(h, addr):
    data = read_mem(h, addr, 8)
    return struct.unpack('<Q', data)[0] if data else 0

def read_u32(h, addr):
    data = read_mem(h, addr, 4)
    return struct.unpack('<I', data)[0] if data else 0

def main():
    h = kernel32.OpenProcess(0x10, False, 73956)
    base = 0x7FF75ED50000
    col_addr = 0x7FF75FD97D24
    raw = read_mem(h, col_addr, 32)
    sig, offset, cdOffset, td_rva, cd_rva, self_rva = struct.unpack('<IIIIII', raw[:24])
    print(f"COL: sig={sig}, offset={offset}, cdOffset={cdOffset}, td_rva=0x{td_rva:X}, cd_rva=0x{cd_rva:X}, self_rva=0x{self_rva:X}")
    # td is at base + td_rva
    td_addr = base + td_rva
    td_name = read_mem(h, td_addr + 16, 64)
    print(f"TypeName: {td_name.split(b'\\x00')[0].decode('latin-1', errors='ignore')}")

if __name__ == '__main__':
    main()
