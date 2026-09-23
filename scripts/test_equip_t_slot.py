import json

with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
    def call(m, p={}):
        pipe.write((json.dumps({"jsonrpc": "2.0", "id": 1, "method": m, "params": p}) + "\n").encode("utf-8"))
        pipe.flush()
        return json.loads(pipe.readline())

    print("=== Equipping PERDITION_BEAM_TITAN on Titan 1606 ===")
    res = call("update_ship_design", {
        "design_id": 1606,
        "name": "天罚",
        "slots": [
            {"section_name": "bow", "slot_name": "TITANIC_01", "component_key": "PERDITION_BEAM_TITAN"}
        ]
    })
    print("Result:", json.dumps(res, ensure_ascii=False, indent=2))

    print("\n=== Verifying Section 0 'bow' ===")
    d_res = call("get_ship_designs", {"design_id": 1606})
    titan = d_res["result"]["designs"][0]
    for slot in titan["sections"][0]["slots"]:
        print(f"  Slot {slot['slot_index']} [{slot['slot_name']}]: {slot['component_key']} ({slot.get('component_name')})")
