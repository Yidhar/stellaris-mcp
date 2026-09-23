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

c_leader_mgr = 0x178BED036E0
raw = read_bytes(c_leader_mgr, 0x800)

target_ids = [93, 97, 100, 103, 167772193]

print("Searching c_leader_mgr for hired leaders (93, 97, 100, 103, ruler):")
for off in range(0, 0x800, 4):
    val = struct.unpack('<I', raw[off:off+4])[0]
    if val in target_ids:
        print(f"  Found ID {val} at c_leader_mgr + {hex(off)}")

# What about player country?
country_mgr = struct.unpack('<Q', read_bytes(base + 0x3287788, 8))[0]
arr = struct.unpack('<Q', read_bytes(country_mgr + 0x18, 8))[0]
player_country = struct.unpack('<Q', read_bytes(arr + 8, 8))[0]

raw_c = read_bytes(player_country, 0x4000)
print("\nSearching player_country for hired leaders:")
for off in range(0, 0x4000, 4):
    val = struct.unpack('<I', raw_c[off:off+4])[0]
    if val in target_ids and val != 167772193: # ruler was already at 0x1d80
        print(f"  Found ID {val} at country + {hex(off)}")

