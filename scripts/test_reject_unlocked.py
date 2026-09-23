import json

with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
    # Attempt to equip LARGE_DRAGON_ARMOR
    req = {
        'jsonrpc': '2.0',
        'id': 1,
        'method': 'update_ship_design',
        'params': {
            'design_id': 1606,
            'slots': [
                {'section_name': 'bow', 'slot_index': 0, 'component_key': 'LARGE_DRAGON_ARMOR'}
            ]
        }
    }
    pipe.write((json.dumps(req) + '\n').encode('utf-8'))
    pipe.flush()
    res = json.loads(pipe.readline())
    print("Result when equipping unresearched Dragon Armor:")
    print(json.dumps(res, ensure_ascii=False, indent=2))
