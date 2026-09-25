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
    # In x64 MSVC:
    # vt - 8 points to RTTICompleteObjectLocator
    col_addr = rp64(vt_addr - 8)
    if not col_addr or col_addr < base:
        return 'No COL'
    # RTTICompleteObjectLocator layout:
    # +0x00: signature (0 for x86, 1 for x64)
    # +0x04: offset
    # +0x08: cdOffset
    # +0x0C: pTypeDescriptor (RVA)
    # +0x10: pClassDescriptor (RVA)
    # +0x14: pSelf (RVA of COL)
    sig = rp32(col_addr)
    td_rva = rp32(col_addr + 0x0C)
    td_addr = base + td_rva
    # TypeDescriptor:
    # +0x00: pVFTable (type_info vftable)
    # +0x08: _data (unused)
    # +0x10: _name (mangled name string, e.g. .?AVClassName@@)
    buf = (ctypes.c_char * 128)()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(td_addr + 0x10), buf, 128, None)
    raw = bytes(buf).split(b'\x00')[0].decode('utf-8', errors='ignore')
    return raw

for off, name in [
    (0x239a720, '0x239a720 (items in 0x3112F78)'),
    (0x239fb40, '0x239fb40 (items in 0x3113148)'),
    (0x23c09f8, '0x23c09f8 (cmd vtable)'),
    (0x2391298, '0x2391298 (buildable bldg vtable)'),
]:
    print(f'{name} -> {get_class_name(base + off)}')
