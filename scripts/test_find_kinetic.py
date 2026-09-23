import json

with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
    req = {'jsonrpc': '2.0', 'id': 1, 'method': 'get_ship_design_catalog', 'params': {'unlocked_only': False}}
    pipe.write((json.dumps(req) + '\n').encode('utf-8'))
    pipe.flush()
    res = json.loads(pipe.readline())['result']['components']
    for c in res:
        for v in c.get('variants', []):
            k = v.get('component_key', '')
            n = v.get('name', '')
            is_u = v.get('is_unlocked', False)
            if "KINETIC" in k or "ARTILLERY" in k:
                print(f"{k} [unlocked={is_u}] -> '{n}'")
