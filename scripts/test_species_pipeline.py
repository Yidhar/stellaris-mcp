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
    print("=== Phase 4.5 Species & Pops Validation Suite ===")
    print("==================================================")
    
    with open(PIPE_NAME, "r+b", buffering=0) as pipe:
        # 1. Layer 1: get_status species summary
        print("\n--- 1. Testing Layer 1: get_status species summary ---")
        status_resp = send_ipc(pipe, {"jsonrpc": "2.0", "id": 1, "method": "get_status"})
        result = status_resp.get("result", {})
        species_summary = result.get("species", {})
        print("Species Summary in Status:")
        print(json.dumps(species_summary, indent=2, ensure_ascii=False))
        
        assert "founder_species_id" in species_summary, "Missing founder_species_id"
        assert "founder_species_name" in species_summary, "Missing founder_species_name"
        assert "total_empire_pops" in species_summary, "Missing total_empire_pops"
        print(f"[+] Layer 1 PASSED: Founder: {species_summary.get('founder_species_name')} (ID: {species_summary.get('founder_species_id')}), Empire Pops: {species_summary.get('total_empire_pops')}, Galaxy Species: {species_summary.get('total_species_in_galaxy')}")

        # 2. Layer 2: get_species (empire mode)
        print("\n--- 2. Testing Layer 2: get_species (empire mode) ---")
        sp_resp = send_ipc(pipe, {"jsonrpc": "2.0", "id": 2, "method": "get_species", "params": {"mode": "empire"}})
        sp_data = sp_resp.get("result", {})
        print(f"Empire Species Count: {sp_data.get('species_count')}")
        species_list = sp_data.get("species", [])
        assert len(species_list) > 0, "Empire species list is empty!"
        
        founder = species_list[0]
        print("\nFounder Species Detail:")
        print(f"  ID: {founder.get('species_id')}")
        print(f"  Key: {founder.get('key')}")
        print(f"  Name: {founder.get('name')}")
        print(f"  Class: {founder.get('class')}")
        print(f"  Traits ({len(founder.get('traits', []))}): {[t['name'] + ' (' + t['key'] + ')' for t in founder.get('traits', [])]}")
        print(f"  Rights Configuration:")
        for cat, r in founder.get('rights', {}).items():
            if isinstance(r, dict):
                print(f"    - {cat:24s}: {r.get('name')} ({r.get('key')})")
            else:
                print(f"    - {cat:24s}: {r}")

        assert len(founder.get('traits', [])) >= 4, "Expected at least 4 traits for Humans!"
        assert founder.get('rights', {}).get('citizenship', {}).get('key') == "citizenship_full", "Expected citizenship_full!"

        catalog = sp_data.get("available_rights_catalog", {})
        print(f"\nAvailable Rights Catalog Categories ({len(catalog)}): {list(catalog.keys())}")
        for cat, opts in catalog.items():
            print(f"  - {cat:24s}: {len(opts)} options (e.g. {opts[0]['name'] if opts else 'none'})")
        assert len(catalog) == 9, f"Expected 9 categories in catalog, got {len(catalog)}!"
        print("[+] Layer 2 (Empire Mode) PASSED!")

        # 3. Layer 2: get_species (galaxy mode)
        print("\n--- 3. Testing Layer 2: get_species (galaxy mode) ---")
        gal_resp = send_ipc(pipe, {"jsonrpc": "2.0", "id": 3, "method": "get_species", "params": {"mode": "galaxy"}})
        gal_data = gal_resp.get("result", {})
        print(f"Galaxy Species Count: {gal_data.get('species_count')}")
        assert gal_data.get("species_count") >= 40, f"Expected >= 40 galaxy species, got {gal_data.get('species_count')}"
        print("[+] Layer 2 (Galaxy Mode) PASSED!")

        # 4. Layer 3: set_species_rights validation
        print("\n--- 4. Testing Layer 3: set_species_rights validation ---")
        founder_id = founder.get("species_id")
        
        # 4a. Invalid category test
        inv_resp = send_ipc(pipe, {
            "jsonrpc": "2.0", "id": 4, "method": "set_species_rights",
            "params": {"species_id": founder_id, "category": "non_existent", "right_value": "test"}
        })
        print("Invalid category response:", json.dumps(inv_resp.get("result", {}), ensure_ascii=False))
        assert inv_resp.get("result", {}).get("success") is False, "Expected failure on invalid category!"

        # 4b. Invalid right value test
        inv_val_resp = send_ipc(pipe, {
            "jsonrpc": "2.0", "id": 5, "method": "set_species_rights",
            "params": {"species_id": founder_id, "category": "citizenship", "right_value": "invalid_val_123"}
        })
        print("Invalid value response:", json.dumps(inv_val_resp.get("result", {}), ensure_ascii=False))
        assert inv_val_resp.get("result", {}).get("success") is False, "Expected failure on invalid right value!"

        # 4c. Valid rights modification dispatch
        col_opts = [o["key"] for o in catalog.get("colonization_controls", [])]
        print(f"Colonization control options: {col_opts}")
        cur_col = founder.get("rights", {}).get("colonization_controls", {}).get("key")
        target_col = "colonization_control_yes" if cur_col == "colonization_control_no" else "colonization_control_no"
        
        print(f"Attempting dispatch: colonization_controls {cur_col} -> {target_col} for species {founder_id}")
        cmd_resp = send_ipc(pipe, {
            "jsonrpc": "2.0", "id": 6, "method": "set_species_rights",
            "params": {"species_id": founder_id, "category": "colonization_controls", "right_value": target_col}
        })
        print("Command response:", json.dumps(cmd_resp.get("result", {}), ensure_ascii=False))
        assert cmd_resp.get("result", {}).get("success") is True, f"Failed to dispatch set_species_rights: {cmd_resp}"
        print("[+] Layer 3 Dispatch PASSED!")

    print("\n==================================================")
    print("=== All Phase 4.5 Tests Passed Successfully! ===")
    print("==================================================")

if __name__ == "__main__":
    main()
