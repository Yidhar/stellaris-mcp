import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject, time, json

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

def get_player_stockpiles():
    cmgr = rp(base + 0x3112F50)
    carr = rp(cmgr + 0x18)
    player = rp(carr + 8)
    res_db = rp(base + 0x3150E78)
    cnt = ru32(res_db + 0x14)
    arr = rp(res_db + 8)
    
    bal_ptr = rp(player + 0x2B40)
    stock_arr = rp(bal_ptr + 0x30)
    
    stocks = {}
    for i in range(cnt):
        res_ptr = rp(arr + i * 8)
        k = read_pdx_string(res_ptr + 0x30)
        v_raw = ctypes.c_int64()
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(stock_arr + i * 8), ctypes.byref(v_raw), 8, None)
        stocks[k] = v_raw.value / 100000.0
    return stocks

PIPE_PATH = r"\\.\pipe\stellaris_mcp_bridge"
with open(PIPE_PATH, "r+b", buffering=0) as pipe:
    before = get_player_stockpiles()
    
    # Sell 500 energy
    msg = {"jsonrpc": "2.0", "method": "market_trade", "params": {"resource": "energy", "action": "sell", "units": 500}, "id": 1}
    pipe.write((json.dumps(msg) + "\n").encode())
    pipe.flush()
    print("Trade sell resp:", pipe.readline().decode().strip())
    
    # Tick
    pipe.write(b'{"jsonrpc":"2.0","method":"set_paused","params":{"paused":false},"id":2}\n')
    pipe.readline()
    time.sleep(0.5)
    pipe.write(b'{"jsonrpc":"2.0","method":"set_paused","params":{"paused":true},"id":3}\n')
    pipe.readline()
    
    after = get_player_stockpiles()
    print("\nDiff after SELL energy:")
    for k in before:
        diff = after[k] - before[k]
        if abs(diff) > 0.001:
            print(f"  {k}: before={before[k]}, after={after[k]}, diff={diff}")
            
    # Now BUY 500 energy
    before = after
    msg = {"jsonrpc": "2.0", "method": "market_trade", "params": {"resource": "energy", "action": "buy", "units": 500}, "id": 4}
    pipe.write((json.dumps(msg) + "\n").encode())
    pipe.flush()
    print("\nTrade buy resp:", pipe.readline().decode().strip())
    
    # Tick
    pipe.write(b'{"jsonrpc":"2.0","method":"set_paused","params":{"paused":false},"id":5}\n')
    pipe.readline()
    time.sleep(0.5)
    pipe.write(b'{"jsonrpc":"2.0","method":"set_paused","params":{"paused":true},"id":6}\n')
    pipe.readline()
    
    after2 = get_player_stockpiles()
    print("\nDiff after BUY energy:")
    for k in before:
        diff = after2[k] - before[k]
        if abs(diff) > 0.001:
            print(f"  {k}: before={before[k]}, after={after2[k]}, diff={diff}")
