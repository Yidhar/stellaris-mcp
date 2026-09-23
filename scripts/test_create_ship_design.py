import json
import time
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
        except Exception as e:
            time.sleep(0.05)
    
    if handle is None:
        raise RuntimeError(f"Could not connect to named pipe {PIPE_NAME}")

    win32pipe.SetNamedPipeHandleState(handle, win32pipe.PIPE_READMODE_BYTE, None, None)
    win32file.WriteFile(handle, raw)
    
    # Read response
    chunks = []
    while True:
        hr, data = win32file.ReadFile(handle, 65536)
        chunks.append(data)
        if b'\n' in data or len(data) == 0:
            break
    win32file.CloseHandle(handle)
    resp_text = b"".join(chunks).decode('utf-8').strip()
    return json.loads(resp_text)

def test_create_lifecycle():
    print("=== Step 1: Initial State Check ===")
    res_init = send_ipc("get_ship_designs")
    assert "result" in res_init, f"Initial query failed: {res_init}"
    init_designs = res_init["result"]["designs"]
    init_count = res_init["result"]["count"]
    print(f"[+] Current customizable designs count: {init_count}")
    for d in init_designs:
        print(f"    - ID: {d['design_id']:10d} | Size: {d['ship_size']:22s} | Name: '{d['name']}'")
    assert init_count == 2, f"Expected 2 designs initially, got {init_count}"

    base_corvette = next((d for d in init_designs if d["ship_size"] == "corvette"), None)
    assert base_corvette is not None, "Base corvette design not found"
    base_id = base_corvette["design_id"]

    print("\n=== Step 2: Create Brand-New Corvette Design ===")
    new_name = "HUMAN1_SHIP_Spectre"
    print(f"[*] Calling create_ship_design for size 'corvette', name '{new_name}'...")
    create_res = send_ipc("create_ship_design", {
        "ship_size": "corvette",
        "name": new_name,
        "slots": [
            { "slot_index": 0, "component_key": "SMALL_RED_LASER" },
            { "slot_index": 1, "component_key": "SMALL_MASS_DRIVER_1" },
            { "slot_index": 2, "component_key": "SMALL_RED_LASER" },
            { "slot_index": 3, "component_key": "SMALL_SHIELD_1" },
            { "slot_index": 4, "component_key": "SMALL_ARMOR_1" },
            { "slot_index": 5, "component_key": "SMALL_SHIELD_1" },
            { "slot_index": 6, "component_key": "REACTOR_BOOSTER_1" }
        ]
    })
    print(f"[+] Create Response: {json.dumps(create_res, ensure_ascii=False)}")
    assert "result" in create_res, f"Create failed: {create_res}"
    create_data = create_res["result"]
    assert create_data.get("success") is True, f"Create not successful: {create_data}"
    new_id = create_data["design_id"]
    print(f"[+] New Ship Design ID allocated: {new_id}")
    assert new_id != base_id, f"New ID {new_id} should not equal base ID {base_id}"

    print("\n=== Step 3: Verify Empire Design Roster Now Contains 3 Designs ===")
    res_after = send_ipc("get_ship_designs")
    after_designs = res_after["result"]["designs"]
    after_count = res_after["result"]["count"]
    print(f"[+] Designs count after creation: {after_count}")
    for d in after_designs:
        print(f"    - ID: {d['design_id']:10d} | Size: {d['ship_size']:22s} | Name: '{d['name']}'")
    assert after_count == 3, f"Expected exactly 3 designs, got {after_count}"

    # Inspect the newly created design
    created_d = next((d for d in after_designs if d["design_id"] == new_id), None)
    assert created_d is not None, f"Newly created design {new_id} not found in roster!"
    print(f"\n[+] Detailed Inspection of Newly Created Design {new_id}:")
    print(f"    Name: {created_d['name']}")
    print(f"    Ship Size: {created_d['ship_size']}")
    print(f"    Sections & Slots:")
    for sec in created_d["sections"]:
        print(f"      Section '{sec['name']}':")
        for sl in sec["slots"]:
            print(f"        [{sl['slot_index']}] {sl['slot_name']:18s} -> {sl['component_key']}")
    
    # Verify slot 0 is SMALL_RED_LASER
    assert created_d["sections"][0]["slots"][0]["component_key"] == "SMALL_RED_LASER"

    print("\n=== Step 4: Delete the Newly Created Ship Design ===")
    print(f"[*] Calling delete_ship_design for ID {new_id}...")
    del_res = send_ipc("delete_ship_design", { "design_id": new_id })
    print(f"[+] Delete Response: {json.dumps(del_res, ensure_ascii=False)}")
    assert del_res.get("result", {}).get("success") is True, f"Delete failed: {del_res}"

    print("\n=== Step 5: Verify Empire Design Roster Restores to 2 Designs ===")
    res_final = send_ipc("get_ship_designs")
    final_count = res_final["result"]["count"]
    print(f"[+] Final designs count: {final_count}")
    for d in res_final["result"]["designs"]:
        print(f"    - ID: {d['design_id']:10d} | Size: {d['ship_size']:22s} | Name: '{d['name']}'")
    assert final_count == 2, f"Expected 2 designs after deletion, got {final_count}"
    assert not any(d["design_id"] == new_id for d in res_final["result"]["designs"]), "Deleted design still in roster!"

    print("\n=======================================================")
    print("[SUCCESS] FULL CREATE SHIP DESIGN LIFECYCLE PASSED 100%!")
    print("=======================================================")

if __name__ == "__main__":
    test_create_lifecycle()
