import ctypes
from ctypes import wintypes
import sys

PROCESS_ALL_ACCESS = 0x1F0FFF

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory

def get_process_by_name(name):
    import win32process
    import win32api
    pids = win32process.EnumProcesses()
    for pid in pids:
        try:
            hProc = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
            mods = win32process.EnumProcessModules(hProc)
            mod_name = win32process.GetModuleFileNameEx(hProc, mods[0])
            if name.lower() in mod_name.lower():
                return pid, mods[0]
        except Exception:
            continue
    return None, None

pid, base = get_process_by_name("stellaris.exe")
print(f"Stellaris PID: {pid}, Base: {hex(base)}")

hProc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
if not hProc:
    print("Failed to open process")
    sys.exit(1)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    bytesRead = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(bytesRead)):
        return buf.raw
    return None

def read_u32(addr):
    raw = read_bytes(addr, 4)
    if raw:
        import struct
        return struct.unpack("<I", raw)[0]
    return 0

def read_u64(addr):
    raw = read_bytes(addr, 8)
    if raw:
        import struct
        return struct.unpack("<Q", raw)[0]
    return 0

def read_pdx_string(addr):
    # PdxString: [0..15] buf, +0x10 size, +0x18 capacity
    # If cap >= 16, heap_ptr at +0x00
    cap = read_u64(addr + 0x18)
    size = read_u64(addr + 0x10)
    if size == 0 or size > 1024:
        return ""
    if cap < 16:
        raw = read_bytes(addr, size)
        if raw:
            return raw.decode('utf-8', errors='ignore')
    else:
        ptr = read_u64(addr)
        if ptr:
            raw = read_bytes(ptr, size)
            if raw:
                return raw.decode('utf-8', errors='ignore')
    return ""

# Inspect leader_mgr
leader_mgr = read_u64(base + 0x3287320)
print(f"leader_mgr: {hex(leader_mgr)}")
if leader_mgr:
    tbl = read_u64(leader_mgr + 0x18)
    cap = read_u32(leader_mgr + 0x20)
    count = read_u32(leader_mgr + 0x24) # check if +0x24 or +0x28 is size
    print(f"Leader table: {hex(tbl)}, cap: {cap}, count?: {count}")
    
    # Let's inspect first 20 valid entries in table
    valid_leaders = []
    for i in range(cap):
        ptr = read_u64(tbl + i * 16 + 8)
        if ptr:
            lid = read_u32(ptr + 0x20)
            valid_leaders.append((i, ptr, lid))
    
    print(f"Total valid leaders in global table: {len(valid_leaders)}")
    for i, ptr, lid in valid_leaders[:15]:
        name_key = read_pdx_string(ptr + 0x50)
        level = read_u32(ptr + 0xD0)
        age = read_u32(ptr + 0x108)
        cls_ptr = read_u64(ptr + 0xE0)
        cls_key = read_pdx_string(cls_ptr + 0x20) if cls_ptr else ""
        print(f"  Slot {i}: Ptr={hex(ptr)}, ID={lid}, NameKey='{name_key}', Class='{cls_key}', Lv={level}, Age={age}")

