import win32process, ctypes

PROCESS_ALL_ACCESS = 0x1F0FFF
kernel32 = ctypes.windll.kernel32
pid = 96948
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
base = win32process.EnumProcessModules(h_proc)[0]

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, size, ctypes.byref(n)):
        return bytes(buf.raw[:n.value])
    return None

def read_u64(addr):
    b = read_bytes(addr, 8)
    return int.from_bytes(b, 'little') if b else 0

def read_u32(addr):
    b = read_bytes(addr, 4)
    return int.from_bytes(b, 'little') if b else 0

leader_95 = 0x2181aaf3908
pname = leader_95 + 0x38

print('=== Inspecting pname (leader + 0x38) ===')
for off in range(0, 0x80, 8):
    v = read_u64(pname + off)
    print(f'+{hex(off)}: {hex(v)}')

var_arr = read_u64(pname + 0x48)
var_cnt = read_u32(pname + 0x54) # or 0x50/0x58
print(f'var_arr: {hex(var_arr)}, var_cnt (at 0x54): {var_cnt}, at 0x50: {read_u32(pname+0x50)}')

if var_arr:
    for i in range(min(5, max(var_cnt, 2))):
        base_item = var_arr + i * 0x40
        print(f'Item {i} at {hex(base_item)}:')
        for o in range(0, 0x40, 8):
            print(f'  +{hex(o)}: {hex(read_u64(base_item + o))}')
        val_ptr = read_u64(base_item + 0x38)
        print(f'  val_ptr (+0x38): {hex(val_ptr)}')
        if val_ptr:
            # Let s inspect val_ptr
            for vo in range(0, 0x40, 8):
                print(f'    val+{hex(vo)}: {hex(read_u64(val_ptr + vo))}')
