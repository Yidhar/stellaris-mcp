import json
import time

PIPE_PATH = r"\\.\pipe\stellaris_mcp_bridge"

def send_request(pipe, req_id: int, method: str, params: dict | None = None) -> dict:
    msg = {
        "jsonrpc": "2.0",
        "method": method,
        "params": params or {},
        "id": req_id
    }
    line = json.dumps(msg) + "\n"
    pipe.write(line.encode("utf-8"))
    pipe.flush()

    resp_line = pipe.readline().decode("utf-8").strip()
    return json.loads(resp_line)

def main():
    print("[*] Connecting to Stellaris Bridge...")
    with open(PIPE_PATH, "r+b", buffering=0) as pipe:
        print("[+] Connected!")

        resp = send_request(pipe, 1, "ping")
        print("Ping:", resp)

        # 1. Create template
        print("\n--- 1. Creating template ---")
        custom_traits = ["trait_adaptive", "trait_unruly", "trait_intelligent"]
        create_resp = send_request(pipe, 2, "create_species_template", {
            "base_species_id": 3019898881,
            "name": "Homo Sapiens Delta",
            "traits": custom_traits
        })
        print("Create Response:", json.dumps(create_resp, indent=2, ensure_ascii=False))

        time.sleep(1.0)

        # 2. Get species list
        print("\n--- 2. Fetching species list ---")
        sp_resp = send_request(pipe, 3, "get_species", {"mode": "empire"})
        species_list = sp_resp.get("result", {}).get("species", [])
        
        target = None
        for s in species_list:
            print(f"  - ID: {s.get('species_id')}, Base: {s.get('base_species_id')}, Template: {s.get('is_template')}, Name: {s.get('name')}")
            if s.get("name") == "Homo Sapiens Delta" or s.get("key") == "Homo Sapiens Delta":
                target = s

        if not target:
            print("[-] Target template not found in species list!")
            return

        tmpl_id = target["species_id"]
        print(f"[+] Found created template ID: {tmpl_id}")

        # 3. Test modify
        print(f"\n--- 3. Testing modify on template ID {tmpl_id} ---")
        mod_resp = send_request(pipe, 4, "modify_species_template", {
            "template_species_id": tmpl_id,
            "name": "Homo Sapiens Omega",
            "traits": ["trait_adaptive", "trait_unruly", "trait_rapid_breeders"]
        })
        print("Modify Response:", json.dumps(mod_resp, indent=2, ensure_ascii=False))

        time.sleep(1.0)

        # 4. Test delete
        print(f"\n--- 4. Testing delete on template ID {tmpl_id} ---")
        del_resp = send_request(pipe, 5, "delete_species_template", {
            "species_id": tmpl_id
        })
        print("Delete Response:", json.dumps(del_resp, indent=2, ensure_ascii=False))

        time.sleep(1.0)

        # 5. Verify deleted
        print("\n--- 5. Verifying deletion ---")
        sp_resp2 = send_request(pipe, 6, "get_species", {"mode": "empire"})
        species_list2 = sp_resp2.get("result", {}).get("species", [])
        still_exists = any(s.get("species_id") == tmpl_id for s in species_list2)
        print(f"Template still exists: {still_exists}")

if __name__ == "__main__":
    main()
