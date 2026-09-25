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

        # 1. Ping
        resp = send_request(pipe, 1, "ping")
        print("Ping:", resp)

        # 2. Get species modification info for Founder Human (3019898881)
        print("\n--- 1. Testing get_species_modification_info for Human (3019898881) ---")
        resp = send_request(pipe, 2, "get_species_modification_info", {"species_id": 3019898881})
        info = resp.get("result", {})
        print("Modification Info:")
        print(f"  Species: {info.get('name')} (ID: {info.get('species_id')})")
        print(f"  Class: {info.get('class')}")
        print(f"  Points: Total={info.get('points_total')}, Used={info.get('points_used')}, Free={info.get('points_free')}")
        print(f"  Picks:  Total={info.get('picks_total')}, Used={info.get('picks_used')}, Free={info.get('picks_free')}")
        print(f"  Current Traits ({len(info.get('current_traits', []))}):")
        for t in info.get("current_traits", []):
            print(f"    - {t['name']} ({t['key']}) [Cost: {t['cost']}]")
        print(f"  Available Traits in Catalog: {info.get('available_traits_catalog_count')}")

        # 3. Create a new species template
        # Base human has: trait_adaptive (+2), trait_nomadic (+1), trait_wasteful (-1) = 2 points used, 3 picks used
        # Let's create a template: replace trait_wasteful with trait_unruly (-2) and add trait_intelligent (+2)
        # Net cost: adaptive(2) + nomadic(1) + unruly(-2) + intelligent(2) = 3 points used -> wait, points_total is 2
        # So let's use: adaptive(2) + nomadic(1) + wasteful(-1) -> exactly 2 points, but custom name "Homo Sapiens Plus"!
        print("\n--- 2. Testing create_species_template ---")
        tmpl_traits = ["trait_adaptive", "trait_nomadic", "trait_wasteful", "trait_natural_sociologists"] # 2 + 1 - 1 + 1 = 3? Wait, let's see points_free first
        # If points_free == 0, we can remove wasteful (-1) and add unruly (-2) and natural_sociologists (+1) -> net 2!
        # Let's test: adaptive(+2), unruly(-2), intelligent(+2) = 2 points used, 3 picks!
        custom_traits = ["trait_adaptive", "trait_unruly", "trait_intelligent"]
        print(f"Creating template 'Homo Sapiens Novus' with traits: {custom_traits}")
        create_resp = send_request(pipe, 3, "create_species_template", {
            "base_species_id": 3019898881,
            "name": "Homo Sapiens Novus",
            "traits": custom_traits
        })
        print("Create Template Response:", json.dumps(create_resp, indent=2, ensure_ascii=False))

        time.sleep(0.5)

        # 4. Check get_species to see if the new template is listed
        print("\n--- 3. Checking get_species to verify template created ---")
        sp_resp = send_request(pipe, 4, "get_species", {"mode": "empire"})
        sp_result = sp_resp.get("result", {})
        species_list = sp_result.get("species", [])
        print(f"Empire species count now: {len(species_list)}")
        
        created_template = None
        for s in species_list:
            print(f"  - Species ID: {s.get('species_id')}, Base ID: {s.get('base_species_id')}, Template: {s.get('is_template')}, Name: {s.get('name')}")
            if s.get("name") == "Homo Sapiens Novus" or s.get("key") == "Homo Sapiens Novus" or s.get("base_species_id") == 3019898881 and s.get("is_template"):
                created_template = s

        if created_template:
            print("[+] Successfully found created template!")
            print(json.dumps(created_template, indent=2, ensure_ascii=False))
            tmpl_id = created_template.get("species_id")

            # 5. Test modify_species_template
            print(f"\n--- 4. Testing modify_species_template on template ID {tmpl_id} ---")
            mod_resp = send_request(pipe, 5, "modify_species_template", {
                "template_species_id": tmpl_id,
                "name": "Homo Sapiens Superior",
                "traits": ["trait_adaptive", "trait_unruly", "trait_rapid_breeders"]
            })
            print("Modify Template Response:", json.dumps(mod_resp, indent=2, ensure_ascii=False))

            # 6. Test delete_species_template
            print(f"\n--- 5. Testing delete_species_template on template ID {tmpl_id} ---")
            del_resp = send_request(pipe, 6, "delete_species_template", {
                "species_id": tmpl_id
            })
            print("Delete Template Response:", json.dumps(del_resp, indent=2, ensure_ascii=False))

            time.sleep(0.5)

            # Check get_species again
            sp_resp2 = send_request(pipe, 7, "get_species", {"mode": "empire"})
            sp_result2 = sp_resp2.get("result", {})
            species_list2 = sp_result2.get("species", [])
            print(f"Empire species count after deletion: {len(species_list2)}")
        else:
            print("[-] Template not found in get_species list")

if __name__ == "__main__":
    main()
