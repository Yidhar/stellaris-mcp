import ctypes
import struct

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory
pid = 104400
base = 0x7ff75ed50000
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    bytesRead = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(bytesRead)):
        return buf.raw
    return None

def read_u32(addr):
    raw = read_bytes(addr, 4)
    return struct.unpack("<I", raw)[0] if raw else 0

def read_u64(addr):
    raw = read_bytes(addr, 8)
    return struct.unpack("<Q", raw)[0] if raw else 0

def read_pdx_string(addr):
    cap = read_u64(addr + 0x18)
    size = read_u64(addr + 0x10)
    if size == 0 or size > 1024:
        return ""
    if cap < 16:
        raw = read_bytes(addr, size)
        return raw.decode('utf-8', errors='ignore') if raw else ""
    else:
        ptr = read_u64(addr)
        if ptr:
            raw = read_bytes(ptr, size)
            return raw.decode('utf-8', errors='ignore') if raw else ""
    return ""

leader_mgr = read_u64(base + 0x3287320)
tbl = read_u64(leader_mgr + 0x18)
cap = read_u32(leader_mgr + 0x20)

for i in range(cap):
    ptr = read_u64(tbl + i * 16 + 8)
    if ptr:
        lid = read_u32(ptr + 0x20)
        name_key = read_pdx_string(ptr + 0x50)
        level = read_u32(ptr + 0xD0)
        age = read_u32(ptr + 0x108)
        cls_ptr = read_u64(ptr + 0xE0)
        cls_key = read_pdx_string(cls_ptr + 0x20) if cls_ptr else ""
        species_key = read_pdx_string(ptr + 0xA0) # check species
        # Let's check some fields: +0x10, +0x14, +0x18, +0x1C, +0x24, +0x28, +0x30, +0x38, +0x40, +0x48
        # Look for humans or player leaders
        if "human" in species_key.lower() or "human" in name_key.lower() or "%LEADER" in name_key or "Muwanga" in name_key:
            # print field values
            vals = [read_u32(ptr + off) for off in range(0x10, 0x50, 4)]
            print(f"Slot {i:4d}: ID={lid:10d}, NameKey='{name_key}', Species='{species_key}', Class='{cls_key}', Lv={level}, Age={age}")
            # print all u32 in 0x10..0x50
            # print(f"    0x10..0x4C: {vals}")

