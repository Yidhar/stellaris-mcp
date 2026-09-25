import ctypes
import win32process
import win32api
import capstone

pid = 77180
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, size, ctypes.byref(read))
    return buf.raw[:read.value]

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

code = read_bytes(base + 0x16D6C90, 0x30)
for i in cs.disasm(code, base + 0x16D6C90):
    print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")
