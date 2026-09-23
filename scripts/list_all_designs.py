import json

with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
    req = {"jsonrpc": "2.0", "id": 1, "method": "get_ship_designs", "params": {}}
    pipe.write((json.dumps(req) + "\n").encode("utf-8"))
    pipe.flush()
    res = json.loads(pipe.readline())["result"]["designs"]
    print(f"Total designs: {len(res)}")
    for d in res:
        print(f"ID: {d['design_id']}, Size: {d['ship_size']}, Name: {d['name']}")
