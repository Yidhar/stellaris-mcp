"""End-to-end verification of species modification tools through the MCP stdio interface.
"""

import subprocess
import json
import time

def main():
    cmd = ["node", "d:/stellarismcp/stellaris_mcp_server/dist/index.js"]
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="ignore",
        bufsize=1
    )

    req_id = 0
    def send_rpc(method: str, params: dict) -> dict:
        nonlocal req_id
        req_id += 1
        msg = {"jsonrpc": "2.0", "id": req_id, "method": method, "params": params}
        proc.stdin.write(json.dumps(msg) + "\n")
        proc.stdin.flush()
        line = proc.stdout.readline()
        return json.loads(line.strip())

    print("=== 1. Initialize MCP ===")
    init_res = send_rpc("initialize", {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "test-runner", "version": "1.0"}
    })
    print("Init:", init_res.get("result", {}).get("serverInfo"))
    proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
    proc.stdin.flush()

    print("\n=== 2. Check tools/list ===")
    tools_res = send_rpc("tools/list", {})
    tools = [t["name"] for t in tools_res.get("result", {}).get("tools", [])]
    expected_new = [
        "stellaris_get_species_modification_info",
        "stellaris_create_species_template",
        "stellaris_modify_species_template",
        "stellaris_delete_species_template",
        "stellaris_apply_species_template"
    ]
    for e in expected_new:
        assert e in tools, f"Missing tool: {e}"
        print(f"  [+] Found tool: {e}")

    print("\n=== 3. Test stellaris_get_species_modification_info ===")
    info_res = send_rpc("tools/call", {
        "name": "stellaris_get_species_modification_info",
        "arguments": {"species_id": 3019898881}
    })
    info_text = json.loads(info_res["result"]["content"][0]["text"])
    print("Points total:", info_text.get("points_total"))
    print("Points free:", info_text.get("points_free"))
    print("Picks free:", info_text.get("picks_free"))
    print("Traits catalog count:", info_text.get("available_traits_catalog_count"))

    print("\n=== 4. Test stellaris_create_species_template ===")
    create_res = send_rpc("tools/call", {
        "name": "stellaris_create_species_template",
        "arguments": {
            "base_species_id": 3019898881,
            "name": "Homo Sapiens MCP Alpha",
            "traits": ["trait_intelligent", "trait_unruly", "trait_rapid_breeders"]
        }
    })
    create_text = json.loads(create_res["result"]["content"][0]["text"])
    print("Create:", create_text)
    assert create_text.get("success") == True, "Create template failed!"

    time.sleep(1.0)

    print("\n=== 5. Find created template ID via stellaris_get_species ===")
    sp_res = send_rpc("tools/call", {
        "name": "stellaris_get_species",
        "arguments": {"mode": "empire"}
    })
    sp_text = json.loads(sp_res["result"]["content"][0]["text"])
    tmpl = next((s for s in sp_text.get("species", []) if s.get("name") == "Homo Sapiens MCP Alpha"), None)
    assert tmpl is not None, "Created template not found in species list!"
    tmpl_id = tmpl["species_id"]
    print(f"[+] Found created template ID: {tmpl_id}")

    print(f"\n=== 6. Test stellaris_modify_species_template on ID {tmpl_id} ===")
    mod_res = send_rpc("tools/call", {
        "name": "stellaris_modify_species_template",
        "arguments": {
            "template_species_id": tmpl_id,
            "name": "Homo Sapiens MCP Beta",
            "traits": ["trait_intelligent", "trait_wasteful", "trait_nomadic"]
        }
    })
    mod_text = json.loads(mod_res["result"]["content"][0]["text"])
    print("Modify:", mod_text)
    assert mod_text.get("success") == True, "Modify template failed!"

    time.sleep(1.0)

    print("\n=== 7. Find modified template ID via stellaris_get_species ===")
    sp_res2 = send_rpc("tools/call", {
        "name": "stellaris_get_species",
        "arguments": {"mode": "empire"}
    })
    sp_text2 = json.loads(sp_res2["result"]["content"][0]["text"])
    tmpl2 = next((s for s in sp_text2.get("species", []) if s.get("name") == "Homo Sapiens MCP Beta"), None)
    assert tmpl2 is not None, "Modified template not found in species list!"
    tmpl_id2 = tmpl2["species_id"]
    print(f"[+] Found modified template ID: {tmpl_id2}")

    print(f"\n=== 8. Test stellaris_delete_species_template on ID {tmpl_id2} ===")
    del_res = send_rpc("tools/call", {
        "name": "stellaris_delete_species_template",
        "arguments": {"species_id": tmpl_id2}
    })
    del_text = json.loads(del_res["result"]["content"][0]["text"])
    print("Delete:", del_text)
    assert del_text.get("success") == True, "Delete template failed!"

    time.sleep(1.0)

    print("\n=== 9. Verify template deleted from species list ===")
    sp_res3 = send_rpc("tools/call", {
        "name": "stellaris_get_species",
        "arguments": {"mode": "empire"}
    })
    sp_text3 = json.loads(sp_res3["result"]["content"][0]["text"])
    exists = any(s.get("species_id") == tmpl_id2 for s in sp_text3.get("species", []))
    print(f"Template {tmpl_id2} still exists: {exists}")
    assert not exists, "Template should be deleted!"

    print("\n[+] ALL MCP TOOLS VERIFIED SUCCESSFULLY END-TO-END!")
    proc.terminate()

if __name__ == "__main__":
    main()
