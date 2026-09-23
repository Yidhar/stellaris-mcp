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

def test_pipeline():
    print("=== Step 1: Query All Player Ship Designs ===")
    res = send_ipc("get_ship_designs")
    assert "result" in res, f"Query failed: {res}"
    data = res["result"]
    designs = data.get("designs", [])
    count = data.get("count", 0)
    print(f"[+] Found {count} player ship designs:")
    for d in designs:
        print(f"    - ID: {d['design_id']:3d} | Size: {d['ship_size']:15s} | Name: '{d['name']}'")
    
    assert count >= 2, f"Expected at least 2 customizable designs, got {count}"
    design_sizes = {d["ship_size"] for d in designs}
    assert "corvette" in design_sizes, "Corvette design not found!"
    assert "military_station_small" in design_sizes, "Defense platform (military_station_small) design not found!"
    corvette = next((d for d in designs if d.get("ship_size") == "corvette"), None)
    assert corvette is not None, "Corvette design not found!"
    corvette_id = corvette["design_id"]
    print(f"\n[+] Detailed Inspection of Corvette Design {corvette_id}:")
    print(f"    Name: {corvette['name']}")
    print(f"    Size: {corvette['ship_size']}")
    print(f"    Class Prefix: {corvette['class_prefix']}")
    print(f"    Core Systems:")
    for sys_name, comp in corvette["core_components"].items():
        print(f"      * {sys_name:15s}: {comp}")
    print(f"    Sections & Slots:")
    for sec in corvette["sections"]:
        print(f"      Section '{sec['name']}':")
        for sl in sec["slots"]:
            if sl["slot_name"]:
                print(f"        [{sl['slot_index']}] {sl['slot_name']:18s} -> {sl['component_key']}")

    print("\n=== Step 2: Query Global Component Catalog ===")
    cat_res = send_ipc("get_ship_design_catalog")
    assert "result" in cat_res, f"Catalog query failed: {cat_res}"
    cat_data = cat_res["result"]
    comp_sets = cat_data.get("components", [])
    total_sets = cat_data.get("total_component_sets", 0)
    print(f"[+] Total Component Sets in Database: {total_sets}")
    
    red_laser_set = next((s for s in comp_sets if s["set_key"] == "RED_LASER"), None)
    assert red_laser_set is not None, "RED_LASER set not found in catalog!"
    print(f"[+] Found RED_LASER Set:")
    print(f"    Localized Name: '{red_laser_set['localized_name']}'")
    print(f"    Icon: '{red_laser_set['icon']}'")
    print(f"    Variants:")
    for v in red_laser_set["variants"]:
        print(f"      * {v['component_key']} ({v['size']})")

    print(f"\n=== Step 3: Custom Refit - Update Ship Design {corvette_id} ===")
    orig_slot1_comp = corvette["sections"][0]["slots"][1]["component_key"]
    target_comp = "SMALL_MASS_DRIVER_1" if orig_slot1_comp == "SMALL_RED_LASER" else "SMALL_RED_LASER"
    print(f"[*] Changing Slot 1 ({corvette['sections'][0]['slots'][1]['slot_name']}) from {orig_slot1_comp} to {target_comp}...")
    upd_res = send_ipc("update_ship_design", {
        "design_id": corvette_id,
        "slots": [
            { "slot_index": 1, "component_key": target_comp }
        ]
    })
    print(f"    Update Response: {json.dumps(upd_res, ensure_ascii=False)}")
    assert upd_res.get("result", {}).get("success") is True, f"Update failed: {upd_res}"

    # Verify slot was updated
    verify_res = send_ipc("get_ship_designs", { "design_id": corvette_id })
    v_corvette = verify_res["result"]["designs"][0]
    slot1 = v_corvette["sections"][0]["slots"][1]
    print(f"[+] Verified Slot 1 is now: {slot1['slot_name']} -> {slot1['component_key']}")
    assert slot1["component_key"] == target_comp, f"Expected {target_comp}, got {slot1['component_key']}"

    print("\n=== Step 4: Dispatch Fleet Upgrade Order (Opcode 0x2F93) ===")
    print("[*] Dispatching CFleetUpgradeDesignCommand for Fleet 3 (Military Fleet)...")
    upg_res = send_ipc("upgrade_fleet", {
        "fleet_id": 3
    })
    print(f"    Upgrade Response: {json.dumps(upg_res, ensure_ascii=False)}")
    assert upg_res.get("result", {}).get("success") is True, f"Upgrade failed: {upg_res}"

    print(f"\n=== Step 5: Restore Slot 1 to Original Equipment ({orig_slot1_comp}) ===")
    print(f"[*] Restoring Slot 1 back to {orig_slot1_comp}...")
    rest_res = send_ipc("update_ship_design", {
        "design_id": corvette_id,
        "slots": [
            { "slot_index": 1, "component_key": orig_slot1_comp }
        ]
    })
    assert rest_res.get("result", {}).get("success") is True, f"Restore failed: {rest_res}"
    verify_rest = send_ipc("get_ship_designs", { "design_id": corvette_id })
    rest_slot1 = verify_rest["result"]["designs"][0]["sections"][0]["slots"][1]
    print(f"[+] Verified Slot 1 restored to: {rest_slot1['component_key']}")
    assert rest_slot1["component_key"] == orig_slot1_comp, "Failed to restore slot!"

    print("\n=== Step 6: Test Delete Ship Design (Opcode 0x31B2) ===")
    # Test with invalid design ID first to verify error handling
    del_invalid = send_ipc("delete_ship_design", { "design_id": 999999 })
    print(f"[+] Handled invalid design delete safely: {json.dumps(del_invalid, ensure_ascii=False)}")
    assert "error" in del_invalid, "Expected error for invalid design ID"

    print("\n=== Step 7: Test Rejection of Fixed / Non-Customizable Ship Designs ===")
    # ID 2 is the Constructor fixed template
    upd_fixed = send_ipc("update_ship_design", { "design_id": 2, "name": "Illegal Name" })
    print(f"[+] Safely rejected update on fixed design ID 2: {json.dumps(upd_fixed, ensure_ascii=False)}")
    assert "error" in upd_fixed, "Expected error when updating non-customizable design"
    assert "not a customizable ship design" in upd_fixed["error"]["message"]

    del_fixed = send_ipc("delete_ship_design", { "design_id": 2 })
    print(f"[+] Safely rejected delete on fixed design ID 2: {json.dumps(del_fixed, ensure_ascii=False)}")
    assert "error" in del_fixed, "Expected error when deleting fixed core design"
    assert "fixed core design and cannot be deleted" in del_fixed["error"]["message"]

    print("\n=======================================================")
    print("[SUCCESS] ALL 7 SHIP DESIGNER PIPELINE TESTS PASSED 100%!")
    print("=======================================================")

if __name__ == "__main__":
    test_pipeline()
