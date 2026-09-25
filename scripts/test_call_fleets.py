import json

PIPE_PATH = r"\\.\pipe\stellaris_mcp_bridge"

with open(PIPE_PATH, "r+b", buffering=0) as pipe:
    msg = {"jsonrpc": "2.0", "method": "get_fleets", "params": {"include_civilian": True}, "id": 1}
    pipe.write((json.dumps(msg) + "\n").encode())
    pipe.flush()
    res = json.loads(pipe.readline())
    fleets = res.get("result", {}).get("fleets", [])
    print(f"Total fleets returned: {len(fleets)}")
    for f in fleets[:10]:
        print(f" - Fleet id={f.get('fleet_id')}, name='{f.get('name')}', power={f.get('military_power')}, ships={f.get('total_ships')}")
