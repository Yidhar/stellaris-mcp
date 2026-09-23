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

# Get player country
country_mgr = read_u64(base + 0x3287788)
arr = read_u64(country_mgr + 0x18)
player_country = read_u64(arr + 8) # country 0
print(f"Player country ptr: {hex(player_country)}")

# Ruler ID was 167772193
ruler_id = read_u32(player_country + 0x1D80)
print(f"Player ruler ID: {ruler_id} ({hex(ruler_id)})")

# Find ruler leader pointer in global table
leader_mgr = read_u64(base + 0x3287320)
tbl = read_u64(leader_mgr + 0x18)
cap = read_u32(leader_mgr + 0x20)

ruler_ptr = 0
for i in range(cap):
    ptr = read_u64(tbl + i * 16 + 8)
    if ptr and read_u32(ptr + 0x20) == ruler_id:
        ruler_ptr = ptr
        print(f"Found ruler at slot {i}, ptr = {hex(ptr)}")
        break

if ruler_ptr:
    # Dump 0x00 to 0x200 of ruler
    raw = read_bytes(ruler_ptr, 0x300)
    print("Ruler fields (first 0x300 bytes):")
    for off in range(0, 0x300, 8):
        val64 = struct.unpack("<Q", raw[off:off+8])[0]
        val32_0 = struct.unpack("<I", raw[off:off+4])[0]
        val32_1 = struct.unpack("<I", raw[off+4:off+8])[0]
        extra = ""
        # Check if val64 looks like a pointer or string
        if val32_0 == 0: # country id 0?
            extra += " [val32_0 == 0]"
        print(f"  +{hex(off)}: 0x{val64:016X} | u32_0={val32_0}, u32_1={val32_1}{extra}")

