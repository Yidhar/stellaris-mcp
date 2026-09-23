import json

with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
    req = {'jsonrpc': '2.0', 'id': 1, 'method': 'get_ship_designs', 'params': {'design_id': 1606}}
    pipe.write((json.dumps(req) + '\n').encode('utf-8'))
    pipe.flush()
    res = json.loads(pipe.readline())['result']['designs']
    titan = res[0]
    print("Core components:", json.dumps(titan.get('core_components', {}), indent=2))
