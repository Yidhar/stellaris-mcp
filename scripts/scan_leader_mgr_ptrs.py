import ctypes, struct

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

def r64(addr):
    raw = read_bytes(addr, 8)
    return struct.unpack('<Q', raw)[0] if raw else 0

def r32(addr):
    raw = read_bytes(addr, 4)
    return struct.unpack('<I', raw)[0] if raw else 0

c_leader_mgr = 0x178BED036E0
target_ids = [93, 97, 100, 103, 167772193]

# Check every pointer in c_leader_mgr
for off in range(0, 0x400, 8):
    p = r64(c_leader_mgr + off)
    if 0x17000000000 <= p <= 0x18000000000:
        # Heap pointer! Read 256 bytes from it
        data = read_bytes(p, 256)
        if data:
            for i in range(0, len(data) - 4, 4):
                val = struct.unpack('<I', data[i:i+4])[0]
                if val in target_ids:
                    print(f"Target ID {val} found in buffer pointed by c_leader_mgr + {hex(off)} (buffer {hex(p)}, item off {hex(i)})")

