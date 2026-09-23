import json
import time
import ctypes
import struct
import win32file
import win32pipe

PIPE_NAME = r"\\.\pipe\stellaris_mcp_bridge"

def send_ipc(method, params=None):
    if params is None:
        params = {}
    req = {
        "jsonrpc": "2.0",
        "method": method,
        "params": params,
        "id": int(time.time() * 1000)
    }
    raw = (json.dumps(req) + "\n").encode('utf-8')

    handle = None
    for attempt in range(10):
        try:
            handle = win32file.CreateFile(
                PIPE_NAME,
                win32file.GENERIC_READ | win32file.GENERIC_WRITE,
                0, None,
                win32file.OPEN_EXISTING,
                0, None
            )
            break
        except Exception:
            time.sleep(0.05)
    
    if handle is None:
        raise RuntimeError(f"Could not connect to named pipe {PIPE_NAME}")

    win32pipe.SetNamedPipeHandleState(handle, win32pipe.PIPE_READMODE_BYTE, None, None)
    win32file.WriteFile(handle, raw)
    
    chunks = []
    while True:
        hr, data = win32file.ReadFile(handle, 65536)
        chunks.append(data)
        if b'\n' in data or len(data) == 0:
            break
    win32file.CloseHandle(handle)
    resp_text = b"".join(chunks).decode('utf-8').strip()
    return json.loads(resp_text)

# Memory inspection helpers
import sys
sys.path.append('scripts')
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
h_proc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_u64(addr):
    val = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 8, None)
    return val.value

def read_u32(addr):
    val = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 4, None)
    return val.value

def read_bytes(addr, size):
    buf = bytearray(size)
    n = ctypes.c_size_t()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), (ctypes.c_char * size).from_buffer(buf), size, ctypes.byref(n))
    return bytes(buf[:n.value])

def get_design_ptr(did):
    mgr = read_u64(base + 0x3112980)
    arr = read_u64(mgr + 0x18)
    idx = (did & 0xffffff) * 16 + 8
    return read_u64(arr + idx)

def verify_design_memory(did, expected_name, expected_literal):
    ptr = get_design_ptr(did)
    assert ptr != 0, f"Design {did} pointer is NULL!"
    
    # 1. Check m_name
    cap = read_u64(ptr + 0x68)
    size = read_u64(ptr + 0x60)
    str_ptr = read_u64(ptr + 0x50) if cap >= 16 else (ptr + 0x50)
    name_bytes = read_bytes(str_ptr, size)
    actual_name = name_bytes.decode('utf-8', errors='replace')
    actual_literal = read_bytes(ptr + 0x70, 1)[0]
    
    print(f"  [Memory Check] Design {did} at {hex(ptr)}:")
    print(f"    m_name: '{actual_name}' (size={size}, cap={cap}, literal={actual_literal})")
    assert actual_name == expected_name, f"m_name mismatch! Expected '{expected_name}', got '{actual_name}'"
    assert actual_literal == expected_literal, f"literal mismatch! Expected {expected_literal}, got {actual_literal}"
    
    # 2. Check m_longName and variables
    arr_data = read_u64(ptr + 0xD8)
    arr_cnt = read_u32(ptr + 0xE0)
    print(f"    m_longName variables count: {arr_cnt}")
    assert arr_cnt == 2, f"Expected 2 variables in m_longName, got {arr_cnt}"
    
    # Var 0: NAME
    elem0 = arr_data + 0 * 0x40
    pname0 = read_u64(elem0 + 0x38)
    var_name0 = read_bytes(elem0 + 0x18, 8).split(b'\x00')[0].decode()
    p0_b = read_bytes(pname0, 0x40)
    s0 = int.from_bytes(p0_b[0x28:0x30], 'little')
    c0 = int.from_bytes(p0_b[0x30:0x38], 'little')
    lit0 = p0_b[0x38]
    val0 = read_bytes(int.from_bytes(p0_b[0x18:0x20], 'little') if c0 >= 16 else (pname0 + 0x18), s0).decode('utf-8', errors='replace')
    print(f"    Var 0 ({var_name0}): '{val0}' (literal={lit0})")
    assert var_name0 == "NAME"
    assert val0 == expected_name
    assert lit0 == expected_literal

    # Var 1: SIZE
    elem1 = arr_data + 1 * 0x40
    pname1 = read_u64(elem1 + 0x38)
    var_name1 = read_bytes(elem1 + 0x18, 8).split(b'\x00')[0].decode()
    p1_b = read_bytes(pname1, 0x40)
    s1 = int.from_bytes(p1_b[0x28:0x30], 'little')
    c1 = int.from_bytes(p1_b[0x30:0x38], 'little')
    lit1 = p1_b[0x38]
    val1 = read_bytes(int.from_bytes(p1_b[0x18:0x20], 'little') if c1 >= 16 else (pname1 + 0x18), s1).decode('utf-8', errors='replace')
    print(f"    Var 1 ({var_name1}): '{val1}' (literal={lit1})")
    assert var_name1 == "SIZE"
    print("  [+] Memory structure verification PASSED!")

def test_main():
    print("=== Step 1: Clean up old test design 1599 if present ===")
    res = send_ipc("get_ship_designs")
    designs = res.get("result", {}).get("designs", [])
    for d in designs:
        if d["design_id"] == 1599:
            print("Found old test design 1599, deleting it...")
            send_ipc("delete_ship_design", {"design_id": 1599})
            time.sleep(0.1)

    print("\n=== Step 2: Create new corvette with custom Chinese name '幽灵' ===")
    create_res = send_ipc("create_ship_design", {
        "ship_size": "corvette",
        "name": "幽灵",
        "slots": [
            { "slot_index": 0, "component_key": "SMALL_RED_LASER" },
            { "slot_index": 1, "component_key": "SMALL_MASS_DRIVER_1" }
        ]
    })
    print("Create Response:", json.dumps(create_res, ensure_ascii=False))
    assert create_res.get("result", {}).get("success") is True
    did = create_res["result"]["design_id"]
    print(f"[+] Created design ID: {did}")

    # Verify query returns "幽灵"
    designs_after = send_ipc("get_ship_designs")["result"]["designs"]
    target = next((d for d in designs_after if d["design_id"] == did), None)
    assert target is not None
    assert target["name"] == "幽灵"
    print(f"[+] IPC Query confirmed name is '{target['name']}'")

    # Verify memory structure
    verify_design_memory(did, "幽灵", 1)

    print("\n=== Step 3: Update design name to '先锋' ===")
    upd_res = send_ipc("update_ship_design", {
        "design_id": did,
        "name": "先锋"
    })
    print("Update Response:", json.dumps(upd_res, ensure_ascii=False))
    assert upd_res.get("result", {}).get("success") is True

    # Verify query returns "先锋"
    designs_updated = send_ipc("get_ship_designs")["result"]["designs"]
    target_upd = next((d for d in designs_updated if d["design_id"] == did), None)
    assert target_upd is not None
    assert target_upd["name"] == "先锋"
    print(f"[+] IPC Query confirmed name is now '{target_upd['name']}'")

    # Verify memory structure after rename
    verify_design_memory(did, "先锋", 1)

    print("\n=======================================================")
    print("[SUCCESS] SHIP DESIGN NAME & CalcLongName VERIFICATION 100% PASSED!")
    print("=======================================================")

if __name__ == "__main__":
    test_main()
