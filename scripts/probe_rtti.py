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

def get_class_name(vt):
    col = r64(vt - 8)
    if not col: return "no COL"
    # In x64, COL has type_descriptor_rva at +0x0C
    td_rva = r32(col + 12)
    # type_descriptor is at base + td_rva
    # name is at td + 16
    name_addr = base + td_rva + 16
    buf = ctypes.create_string_buffer(128)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(name_addr), buf, 128, ctypes.byref(read)):
        s = buf.raw[:read.value]
        null_idx = s.find(b'\0')
        if null_idx != -1: s = s[:null_idx]
        return s.decode('ascii', errors='replace')
    return "read err"

vt_list = [
    0x7ff779f9b8f0,
    0x7ff779fa5f08,
    0x7ff779f088a8,
]

for vt in vt_list:
    print(f"VT {hex(vt)}: {get_class_name(vt)}")
