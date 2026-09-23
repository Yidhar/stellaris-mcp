import json

with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
    req = {"jsonrpc": "2.0", "id": 1, "method": "get_ship_designs", "params": {"design_id": 1606}}
    pipe.write((json.dumps(req) + "\n").encode("utf-8"))
    pipe.flush()
    res = json.loads(pipe.readline())["result"]["designs"]
    if not res:
        print("Titan 1606 not found!")
    else:
        titan = res[0]
        print(f"Design ID: {titan['design_id']}, Name: {titan['name']}, Size: {titan['ship_size']}")
        for s_idx, sec in enumerate(titan.get("sections", [])):
            print(f"\n--- Section {s_idx}: '{sec.get('name')}' ---")
            for slot in sec.get("slots", []):
                print(f"  Slot {slot['slot_index']} [{slot.get('slot_name')}]: {slot.get('component_key')} ({slot.get('component_name')})")
