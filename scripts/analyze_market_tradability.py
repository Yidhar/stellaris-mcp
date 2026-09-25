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

def read_u8(addr):
    val = ctypes.c_uint8()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 1, None)
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

bal_ptr = read_ptr(player + 0x2B40)
market_sub = read_ptr(bal_ptr + 0x20)
print(f"bal_ptr: 0x{bal_ptr:X}, market_sub: 0x{market_sub:X}")

# In 0x7FE900:
# [rbx + 0x5c] is count
# [rbx + 0x50] is array of 0x38 bytes entries
m_cnt = read_u32(market_sub + 0x5C)
m_arr = read_ptr(market_sub + 0x50)
print(f"Market entries count: {m_cnt}, array: 0x{m_arr:X}")

res_db = read_ptr(base + 0x3150E78)
cnt = read_u32(res_db + 0x14)
arr = read_ptr(res_db + 0x08)

print(f"\n--- Market Tradability Analysis (Total DB Resources: {cnt}) ---")
for i in range(cnt):
    r_ptr = read_ptr(arr + i * 8)
    if not r_ptr: continue
    k = read_pdx_string(r_ptr + 0x30)
    res_id = read_u32(r_ptr + 0x18)
    base_amt = read_i64(r_ptr + 0x158)
    base_price = read_i64(r_ptr + 0x160)

    is_market_defined = (base_amt > 0 and base_price > 0)
    
    # Check market entry
    flag_30 = 0
    can_trade = False
    if m_arr and res_id < m_cnt:
        entry_addr = m_arr + res_id * 0x38
        flag_30 = read_u8(entry_addr + 0x30)
        # 0x7FE917: test byte ptr [rcx + 0x30], 1 (must be != 0)
        # 0x7FE921: shr al, 3; and al, 1; (must be == 0)
        bit0 = (flag_30 & 1) != 0
        bit3 = (flag_30 & 8) != 0
        can_trade = bit0 and not bit3

    print(f"ID {res_id:2d} | {k:22s} | MarketDef: {str(is_market_defined):5s} | flag_30: 0x{flag_30:02X} | CanTradeNow: {can_trade}")
