import ctypes, struct
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read)):
        return struct.unpack('<I', buf.raw)[0]
    return 0

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

vt = 0x7ff779f088a8
col = r64(vt - 8)
print(f"COL: {hex(col)}")
if col:
    sig = r32(col)
    off = r32(col + 4)
    cd_off = r32(col + 8)
    td_rva = r32(col + 12)
    chd_rva = r32(col + 16)
    ib_rva = r32(col + 20)
    print(f"sig={sig}, off={off}, cd_off={cd_off}, td_rva={hex(td_rva)}, chd_rva={hex(chd_rva)}, ib_rva={hex(ib_rva)}")
    td_addr = base + td_rva
    print(f"td_addr: {hex(td_addr)}")
    buf = ctypes.create_string_buffer(64)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(td_addr + 16), buf, 64, ctypes.byref(read))
    print(f"name: {buf.raw[:read.value]}")
