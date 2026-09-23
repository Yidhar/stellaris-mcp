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

country_mgr = read_u64(base + 0x3287788)
arr = read_u64(country_mgr + 0x18)
player_country = read_u64(arr + 8)

ruler_id = read_u32(player_country + 0x1D80)
print(f"Player country: {hex(player_country)}, Ruler ID: {ruler_id}")

# Read first 0x4000 bytes of player_country to find references to ruler_id
country_bytes = read_bytes(player_country, 0x4000)
for off in range(0, 0x4000, 4):
    val = struct.unpack("<I", country_bytes[off:off+4])[0]
    if val == ruler_id:
        print(f"Ruler ID found at country + {hex(off)}")

# Also let's check std::vector or tree structures around 0x2DE0, 0x2DF8, 0x1D80 etc.
for off in [0x1D80, 0x2DE0, 0x2DF8, 0x2E00, 0x2E18, 0x2E30, 0x2E48]:
    p = read_u64(player_country + off)
    cnt = read_u32(player_country + off + 8)
    cap = read_u32(player_country + off + 12)
    print(f"Country + {hex(off)}: p={hex(p)}, u32_1={cnt}, u32_2={cap}")

