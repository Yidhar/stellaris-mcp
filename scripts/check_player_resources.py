import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def read_ptr(addr):
    val = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 8, None)
    return val.value

def read_u32(addr):
    val = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 4, None)
    return val.value

def read_i64(addr):
    val = ctypes.c_int64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 8, None)
    return val.value

def read_pdx_string(addr):
    cap = read_ptr(addr + 24)
    sz = read_ptr(addr + 16)
    if sz == 0 or sz > 512: return ""
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = read_ptr(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

cmgr = read_ptr(base + 0x3112F50)
carr = read_ptr(cmgr + 0x18)
player = read_ptr(carr + 8)

res_db = read_ptr(base + 0x3150E78)
cnt = read_u32(res_db + 0x14)
arr = read_ptr(res_db + 0x08)

# Check stockpiles
bal_ptr = read_ptr(player + 0x2B40)
stock_arr = read_ptr(bal_ptr + 0x30)

income_arr = read_ptr(player + 0x1E08)

print("--- Player Resources Status ---")
for i in range(cnt):
    r_ptr = read_ptr(arr + i * 8)
    if not r_ptr: continue
    k = read_pdx_string(r_ptr + 0x30)
    base_amt = read_i64(r_ptr + 0x158)
    base_price = read_i64(r_ptr + 0x160)
    if base_amt <= 0 or base_price <= 0: continue

    st = read_i64(stock_arr + i * 8) / 100000.0 if stock_arr else 0.0
    inc = read_i64(income_arr + i * 8) / 100000.0 if income_arr else 0.0
    print(f"Resource {k:16s}: Stockpile={st:8.1f}, Income={inc:8.1f}")
