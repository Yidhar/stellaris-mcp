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

def read_u64(addr):
    raw = read_bytes(addr, 8)
    return struct.unpack("<Q", raw)[0] if raw else 0

def read_u32(addr):
    raw = read_bytes(addr, 4)
    return struct.unpack("<I", raw)[0] if raw else 0

# Note: 0x1435B4270 is based on 0x140000000.
# RVA is 0x35B4270. Live address is base + 0x35B4270.
targets = [
    ("hire_leader", 0x4073, 0x35B4270),
    ("dismiss_leader", 0x4074, 0x35B4390),
    ("assign_leader_command", 0x4076, 0x35B45D0),
    ("change_leader_assignment_command", 0x4292, 0x35DA550),
]

for name, cid, rva in targets:
    addr = base + rva
    raw = read_bytes(addr, 0x80)
    print(f"\n=== {name} (0x{cid:04X}) at {hex(addr)} (RVA 0x{rva:X}) ===")
    if raw:
        for off in range(0, 0x80, 8):
            val64 = struct.unpack("<Q", raw[off:off+8])[0]
            val32_0 = struct.unpack("<I", raw[off:off+4])[0]
            val32_1 = struct.unpack("<I", raw[off+4:off+8])[0]
            extra = ""
            if base < val64 < base + 0x3000000:
                extra = f" -> Live RVA 0x{val64 - base:X}"
            print(f"  +{hex(off)}: 0x{val64:016X} (u32_0={val32_0}, u32_1={val32_1}){extra}")

