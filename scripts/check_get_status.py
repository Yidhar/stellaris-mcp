import json

PIPE_PATH = r"\\.\pipe\stellaris_mcp_bridge"

with open(PIPE_PATH, "r+b", buffering=0) as pipe:
    msg = {"jsonrpc": "2.0", "method": "get_status", "id": 1}
    pipe.write((json.dumps(msg) + "\n").encode())
    pipe.flush()
    res = json.loads(pipe.readline())
    print(json.dumps(res.get("result", {}), indent=2))
