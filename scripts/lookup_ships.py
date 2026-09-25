import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def rp(a):
    v = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 8, None)
    return v.value

def ru32(a):
    v = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 4, None)
    return v.value

def read_pdx_string(addr):
    cap = rp(addr + 24)
    sz = rp(addr + 16)
    if sz == 0 or sz > 512: return ""
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = rp(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

smgr = rp(base + 0x3113010)
s_arr = rp(smgr + 0x18)
s_cap = ru32(smgr + 0x20)

print(f"CShipManager: 0x{smgr:X}, cap={s_cap}")

# Look at Fleet 55
fmgr = rp(base + 0x3113008)
f_arr = rp(fmgr + 0x18)
flt55 = rp(f_arr + (55 & 0xFFFFFF) * 16 + 8)

for off in range(0, 0x400, 8):
    v_ptr = rp(flt55 + off + 8)
    v_cnt = ru32(flt55 + off + 0x14)
    if v_ptr > 0x10000 and v_ptr < 0x7FFFFFFFFFFF and 1 <= v_cnt <= 100:
        # Check first 3 uint32s
        u0 = ru32(v_ptr)
        u1 = ru32(v_ptr + 4)
        print(f"Vector at flt55 + 0x{off:X}: cnt={v_cnt}, u0={u0}, u1={u1}")
        # Look up u0 in CShipManager
        slot = u0 & 0xFFFFFF
        if slot < s_cap:
            ship_obj = rp(s_arr + slot * 16 + 8)
            if ship_obj:
                print(f"   Ship obj: 0x{ship_obj:X}")
                for s_off in range(0, 0x100, 8):
                    s = read_pdx_string(ship_obj + s_off)
                    if s and any(k in s for k in ['science', 'constructor', 'colony', 'corvette', 'destroyer', 'transport']):
                        print(f"       +0x{s_off:X}: '{s}'")
