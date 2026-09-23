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

country_mgr = r64(base + 0x3287788)
arr = r64(country_mgr + 0x18)
player_country = r64(arr + 8)

target_ptrs = [0x179df9eb378, 0x179dfa19098]

print(f"Scanning player_country (0x{player_country:X}) for leader pointers...")
raw_c = read_bytes(player_country, 0x6000)
for off in range(0, 0x6000, 8):
    q = struct.unpack('<Q', raw_c[off:off+8])[0]
    if q in target_ptrs:
        print(f"  Direct ptr to leader found at Country + {hex(off)}")
    elif 0x17000000000 <= q <= 0x18000000000:
        # Check buffer pointed by q
        buf = read_bytes(q, 512)
        if buf:
            for b_off in range(0, len(buf) - 8, 8):
                bq = struct.unpack('<Q', buf[b_off:b_off+8])[0]
                if bq in target_ptrs:
                    print(f"  Indirect ptr to leader found via Country + {hex(off)} -> buffer 0x{q:X} + {hex(b_off)}")

