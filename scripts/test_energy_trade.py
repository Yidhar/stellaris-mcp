import json

PIPE_PATH = r"\\.\pipe\stellaris_mcp_bridge"

def send_request(pipe, req_id: int, method: str, params: dict | None = None) -> dict:
    msg = {
        "jsonrpc": "2.0",
        "method": method,
        "params": params or {},
        "id": req_id
    }
    line = json.dumps(msg) + "\n"
    pipe.write(line.encode("utf-8"))
    pipe.flush()

    resp_line = pipe.readline().decode("utf-8").strip()
    return json.loads(resp_line)

with open(PIPE_PATH, "r+b", buffering=0) as pipe:
    # First get player resources
    res_before = send_request(pipe, 1, "get_status")
    print("Status before:", res_before.get("result", {}).get("energy"))
    
    # Try trading energy!
    trade_res = send_request(pipe, 2, "market_trade", {"resource": "energy", "action": "buy", "units": 100})
    print("Trade result:", trade_res)
