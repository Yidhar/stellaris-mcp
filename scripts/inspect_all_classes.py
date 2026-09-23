import json

with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
    def call(m, p={}):
        pipe.write((json.dumps({"jsonrpc": "2.0", "id": 1, "method": m, "params": p}) + "\n").encode("utf-8"))
        pipe.flush()
        return json.loads(pipe.readline())

    for did in [1601, 1603, 1604, 1605]:
        res = call("get_ship_designs", {"design_id": did})
        d = res["result"]["designs"][0]
        print(f"\n==================================================")
        print(f"Design ID {d['design_id']} | Size: {d['ship_size']} | Name: {d['name']}")
        print(f"Cores: {json.dumps(d['core_components'], ensure_ascii=False)}")
        for s_idx, sec in enumerate(d["sections"]):
            print(f"  Section {s_idx} ('{sec['name']}'):")
            for sl in sec["slots"]:
                print(f"    Slot {sl['slot_index']} [{sl['slot_name']}]: {sl['component_key']} ({sl.get('component_name')})")
