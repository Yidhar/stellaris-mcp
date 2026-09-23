import json

with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
    pipe.write(b'{"jsonrpc":"2.0","id":1,"method":"get_ship_design_catalog","params":{"unlocked_only":false}}\n')
    pipe.flush()
    res = json.loads(pipe.readline())['result']['components']
    for c in res:
        for v in c.get('variants', []):
            k = v.get('component_key', '')
            if 'AURA' in k:
                print(f"{k}: unlocked = {v.get('is_unlocked')}")
