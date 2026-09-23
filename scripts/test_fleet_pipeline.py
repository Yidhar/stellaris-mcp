import json
import time

PIPE_NAME = r"\\.\pipe\stellaris_mcp_bridge"

def send_ipc(pipe, req: dict) -> dict:
    line = json.dumps(req) + "\n"
    pipe.write(line.encode("utf-8"))
    pipe.flush()
    resp_line = pipe.readline().decode("utf-8").strip()
    return json.loads(resp_line)

def main():
    print("==================================================")
    print("=== Phase 5.0 Fleet Manager Validation Suite   ===")
    print("==================================================")

    with open(PIPE_NAME, "r+b", buffering=0) as pipe:
        # 1. Layer 1: get_status fleet summary
        print("\n--- 1. Testing Layer 1: get_status fleet summary ---")
        status_resp = send_ipc(pipe, {"jsonrpc": "2.0", "id": 1, "method": "get_status"})
        result = status_resp.get("result", {})
        fleet_summary = result.get("fleets", {})
        print("Fleet Summary in Status:")
        print(json.dumps(fleet_summary, indent=2, ensure_ascii=False))

        assert "military_fleets_count" in fleet_summary, "Missing military_fleets_count"
        assert "civilian_fleets_count" in fleet_summary, "Missing civilian_fleets_count"
        assert "total_military_power" in fleet_summary, "Missing total_military_power"
        assert "total_reinforceable_fleets" in fleet_summary, "Missing total_reinforceable_fleets"
        print(f"[+] Layer 1 PASSED: Military Fleets: {fleet_summary.get('military_fleets_count')}, Civilian: {fleet_summary.get('civilian_fleets_count')}, Mil Power: {fleet_summary.get('total_military_power')}, Reinforceable: {fleet_summary.get('total_reinforceable_fleets')}")

        # 2. Layer 2: get_fleets (military)
        print("\n--- 2. Testing Layer 2: get_fleets (military) ---")
        fleets_resp = send_ipc(pipe, {"jsonrpc": "2.0", "id": 2, "method": "get_fleets", "params": {"include_civilian": False}})
        fleets_data = fleets_resp.get("result", {})
        print(f"Military Fleets Count: {fleets_data.get('fleet_count')}")
        fleets_list = fleets_data.get("fleets", [])
        assert len(fleets_list) > 0, "Military fleets list is empty!"

        primary_fleet = fleets_list[0]
        f_id = primary_fleet.get("fleet_id")
        t_id = primary_fleet.get("template_id")
        f_name = primary_fleet.get("name")
        mil_pow = primary_fleet.get("military_power")
        designs = primary_fleet.get("designs", [])
        print(f"\nPrimary Fleet Detail:")
        print(f"  Fleet ID: {f_id}")
        print(f"  Template ID: {t_id}")
        print(f"  Name: {f_name}")
        print(f"  Military Power: {mil_pow}")
        print(f"  Total Ships: {primary_fleet.get('total_ships')}")
        print(f"  Total Quota: {primary_fleet.get('total_quota')}")
        print(f"  Can Reinforce: {primary_fleet.get('can_reinforce')}")
        print(f"  Designs ({len(designs)}):")
        for d in designs:
            print(f"    - Design ID: {d.get('design_id')}, Name: '{d.get('design_name')}', Actual: {d.get('actual_count')}, Quota: {d.get('target_quota')}, Deficit: {d.get('deficit')}")

        assert len(designs) > 0, "No ship designs in primary fleet template!"
        target_design = designs[0]
        target_design_id = target_design.get("design_id")
        original_quota = target_design.get("target_quota")

        # 2b. Layer 2: get_fleets (civilian included)
        print("\n--- 2b. Testing Layer 2: get_fleets with civilian ---")
        civ_resp = send_ipc(pipe, {"jsonrpc": "2.0", "id": 3, "method": "get_fleets", "params": {"include_civilian": True}})
        civ_data = civ_resp.get("result", {})
        all_fleets = civ_data.get("fleets", [])
        print(f"Total Fleets (incl civilian): {len(all_fleets)}")
        assert len(all_fleets) > len(fleets_list), "Civilian fleets were not included!"
        print("[+] Layer 2 PASSED")

        # 3. Layer 3: set_fleet_template_quota (Increase quota by 1)
        new_quota = original_quota + 1
        print(f"\n--- 3. Testing Layer 3: set_fleet_template_quota ({original_quota} -> {new_quota}) ---")
        quota_resp = send_ipc(pipe, {
            "jsonrpc": "2.0",
            "id": 4,
            "method": "set_fleet_template_quota",
            "params": {
                "fleet_id": f_id,
                "design_id": target_design_id,
                "target_quota": new_quota
            }
        })
        print("Set Quota Response:", json.dumps(quota_resp, indent=2, ensure_ascii=False))
        assert quota_resp.get("result", {}).get("success") is True, f"Failed to set quota: {quota_resp}"

        # Verify template quota updated
        verify_resp = send_ipc(pipe, {"jsonrpc": "2.0", "id": 5, "method": "get_fleets", "params": {"fleet_id": f_id}})
        v_fleet = verify_resp.get("result", {}).get("fleets", [])[0]
        v_design = next((d for d in v_fleet.get("designs", []) if d.get("design_id") == target_design_id), None)
        assert v_design is not None, "Target design not found after quota update"
        print(f"Updated Design State: Quota={v_design.get('target_quota')}, Deficit={v_design.get('deficit')}, CanReinforce={v_fleet.get('can_reinforce')}")
        assert v_design.get("target_quota") == new_quota, f"Quota expected {new_quota}, got {v_design.get('target_quota')}"
        assert v_design.get("deficit") == 1, f"Deficit expected 1, got {v_design.get('deficit')}"
        assert v_fleet.get("can_reinforce") is True, "Fleet should now be reinforceable"
        print("[+] Template Quota Modification PASSED")

        # 4. Layer 3: reinforce_fleet
        print(f"\n--- 4. Testing Layer 3: reinforce_fleet (Fleet {f_id}) ---")
        reinf_resp = send_ipc(pipe, {
            "jsonrpc": "2.0",
            "id": 6,
            "method": "reinforce_fleet",
            "params": {"fleet_id": f_id}
        })
        print("Reinforce Response:", json.dumps(reinf_resp, indent=2, ensure_ascii=False))
        assert reinf_resp.get("result", {}).get("success") is True, f"Failed to reinforce: {reinf_resp}"
        print("[+] Native Reinforce Command Dispatched Successfully")

        # 5. Restore quota back
        print(f"\n--- 5. Restoring Template Quota to {original_quota} ---")
        restore_resp = send_ipc(pipe, {
            "jsonrpc": "2.0",
            "id": 7,
            "method": "set_fleet_template_quota",
            "params": {
                "fleet_id": f_id,
                "design_id": target_design_id,
                "target_quota": original_quota
            }
        })
        assert restore_resp.get("result", {}).get("success") is True, "Failed to restore quota"
        print("[+] Quota Restored Successfully")

    print("\n==================================================")
    print("=== ALL FLEET MANAGER TESTS PASSED (100% SUCCESS) ===")
    print("==================================================")

if __name__ == "__main__":
    main()
