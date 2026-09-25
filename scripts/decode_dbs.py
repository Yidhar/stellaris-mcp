import ctypes
import sys
sys.path.append(r'D:\stellarismcp\scripts')
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = ctypes.windll.kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def rp64(addr):
    v = ctypes.c_uint64()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(v), 8, None)
    return v.value

def rp32(addr):
    v = ctypes.c_uint32()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(v), 4, None)
    return v.value

def get_class_name(vt_addr):
    col_addr = rp64(vt_addr - 8)
    if not col_addr:
        return 'No COL'
    # Check if col_addr is pointer or RVA
    if col_addr < base:
        col_addr = base + col_addr
    td_rva = rp32(col_addr + 0x0C)
    td_addr = base + td_rva
    buf = (ctypes.c_char * 128)()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(td_addr + 0x10), buf, 128, None)
    raw = bytes(buf).split(b'\x00')[0].decode('utf-8', errors='ignore')
    return raw

for rva, name in [
    (0x23466C0, 'db1 mgr vt'),
    (0x2347790, 'db2 mgr vt'),
    (0x23465E0, 'db3 mgr vt'),
    (0x23477E8, 'planet mgr vt'),
    (0x2347720, 'solar system mgr vt'),
    (0x2390F30, 'CBuildableClearDepositBlocker vt'),
]:
    print(f"{name} (0x{rva:X}) -> {get_class_name(base + rva)}")

# Now check object vtables inside db1, db2, db3
for db_rva, db_name in [(0x3112FB0, "db1"), (0x3113140, "db2"), (0x3112F50, "db3"), (0x3113128, "planets")]:
    mgr = rp64(base + db_rva)
    table = rp64(mgr + 0x18)
    obj = rp64(table + 8) # slot 0
    if obj:
        vt = rp64(obj)
        print(f"Object in {db_name} (slot 0: 0x{obj:X}, vt: 0x{vt-base:X}) -> {get_class_name(vt)}")

