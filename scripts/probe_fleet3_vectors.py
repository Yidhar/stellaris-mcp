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

# Let's inspect CFleet 3 more deeply
fleet_mgr = r64(base + 0x3113008)
fleet_tbl = r64(fleet_mgr + 0x18)
fleet3 = r64(fleet_tbl + 3 * 16 + 8)

print(f"Fleet 3: {hex(fleet3)}")

# Scan memory around Fleet 3
# In Stellaris, a Fleet has ships.
# Where are the ships stored?
# Could ships be stored as an array of 32-bit ship IDs?
# Let's check all vectors in Fleet 3:
# A vector is (ptr, cap, size) or (begin, end, end_cap)
for off in range(0, 0x200, 8):
    p0 = r64(fleet3 + off)
    p1 = r64(fleet3 + off + 8)
    p2 = r64(fleet3 + off + 16)
    # Check if p0, p1, p2 look like std::vector<uint32_t> or std::vector<void*>
    if p0 > 0x10000 and p1 >= p0 and p1 <= p0 + 0x10000:
        diff = p1 - p0
        print(f"std::vector at +{hex(off)}: begin={hex(p0)}, end={hex(p1)}, byte_size={diff}")
        if diff > 0 and diff < 1000:
            # print items as u32
            u32_items = [r32(p0 + k * 4) for k in range(diff // 4)]
            print(f"   as u32 ({len(u32_items)}): {u32_items}")
            # print items as u64
            u64_items = [r64(p0 + k * 8) for k in range(diff // 8)]
            print(f"   as u64 ({len(u64_items)}): {[hex(x) for x in u64_items]}")
