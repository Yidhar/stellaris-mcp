import json

with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
    req = {'jsonrpc': '2.0', 'id': 1, 'method': 'get_ship_design_catalog', 'params': {'unlocked_only': False}}
    pipe.write((json.dumps(req) + '\n').encode('utf-8'))
    pipe.flush()
    res = json.loads(pipe.readline())['result']['components']
    print(f"Total components in catalog: {len(res)}")
    matches = []
    for c in res:
        for v in c.get('variants', []):
            k = v.get('component_key', '')
            n = v.get('name', '')
            is_u = v.get('is_unlocked', False)
            if any(w in k.lower() for w in ['dragon', 'armor', 'kinetic', 'artillery', 'titan']):
                matches.append(f"{k} [unlocked={is_u}] -> '{n}'")
    print(f"Matching components count: {len(matches)}")
    for m in matches[:50]:
        print(" ", m)
