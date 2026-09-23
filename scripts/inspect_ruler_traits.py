import ctypes, struct

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory
pid = 104400
base = 0x7ff75ed50000
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read)):
        return struct.unpack('<I', buf.raw)[0]
    return 0

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

leader_mgr = r64(base + 0x3287320)
tbl = r64(leader_mgr + 0x18)
cap = r32(leader_mgr + 0x20)

ruler_ptr = 0
for i in range(cap):
    p = r64(tbl + i * 16 + 8)
    if p and r32(p + 0x20) == 167772193:
        ruler_ptr = p
        break

print(f"Ruler ptr: {hex(ruler_ptr)}")

# Let's inspect leader traits!
# Where are traits on ruler and hired leaders?
# Traits are PdxString or vector of trait pointers!
# Let's inspect all pointers in ruler that point to PdxString or object with trait key
for off in range(0, 0x400, 8):
    q = r64(ruler_ptr + off)
    if 0x17000000000 <= q <= 0x18000000000:
        # read first 64 bytes
        buf = ctypes.create_string_buffer(64)
        read = ctypes.c_size_t()
        if ReadProcessMemory(hProc, ctypes.c_void_p(q), buf, 64, ctypes.byref(read)):
            raw = buf.raw
            # check for trait strings like "leader_trait_"
            if b"trait_" in raw or b"leader_" in raw:
                print(f"Trait string at +{hex(off)} -> 0x{q:X}: {raw[:32]}")
            # check if it's a vector of trait pointers:
            # vector: start, end, cap
            p_end = r64(ruler_ptr + off + 8)
            if 0 < p_end - q < 0x200 and (p_end - q) % 8 == 0:
                cnt = (p_end - q) // 8
                print(f"Vector of {cnt} pointers at +{hex(off)}: start=0x{q:X}, end=0x{p_end:X}")
                for idx in range(cnt):
                    elem = r64(q + idx * 8)
                    elem_buf = ctypes.create_string_buffer(64)
                    if ReadProcessMemory(hProc, ctypes.c_void_p(elem), elem_buf, 64, ctypes.byref(read)):
                        print(f"    [{idx}] 0x{elem:X}: {elem_buf.raw[:32]}")

