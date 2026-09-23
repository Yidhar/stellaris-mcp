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

leader_mgr = read_u64(base + 0x3287320)
tbl = read_u64(leader_mgr + 0x18)
cap = read_u32(leader_mgr + 0x20)

country_mgr = read_u64(base + 0x3287788)
arr = read_u64(country_mgr + 0x18)
player_country = read_u64(arr + 8)

leaders = []
for i in range(cap):
    ptr = read_u64(tbl + i * 16 + 8)
    if ptr:
        lid = read_u32(ptr + 0x20)
        leaders.append((i, ptr, lid))

print(f"Total leaders: {len(leaders)}")

# Let's inspect 5 different leaders and print uint32 at various offsets
# to find owner_country_id or owner_country pointer
offsets_to_check = [0x20, 0x24, 0x30, 0x38, 0x40, 0x48, 0xC8, 0xD0, 0xE8, 0xF8, 0x108, 0x138, 0x148, 0x150, 0x218, 0x228, 0x230]

print("Comparing leaders:")
for i, ptr, lid in leaders[:8]:
    # read country ptr if any
    # Check if any pointer in leader points to player_country (0x179e3f67040)
    raw = read_bytes(ptr, 0x400)
    c_ptrs = []
    c_ids = []
    for off in range(0, 0x400, 8):
        q = struct.unpack("<Q", raw[off:off+8])[0]
        if q == player_country:
            c_ptrs.append(hex(off))
    for off in range(0, 0x400, 4):
        u = struct.unpack("<I", raw[off:off+4])[0]
        # if u < 60: maybe country_id
    print(f"Leader {lid} (Slot {i}): country_ptr found at {c_ptrs}")

