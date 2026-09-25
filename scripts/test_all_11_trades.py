import json

PIPE_PATH = r"\\.\pipe\stellaris_mcp_bridge"

resources = [
    "energy",
    "minerals",
    "food",
    "consumer_goods",
    "alloys",
    "volatile_motes",
    "exotic_gases",
    "rare_crystals",
    "sr_living_metal",
    "sr_zro",
    "sr_dark_matter"
]

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
    # First get market resources
    m_info = send_request(pipe, 1, "get_market")["result"]
    print("Market fee:", m_info.get("market_fee_percent"))
    
    print("\n--- Testing trade for each of the 11 resources ---")
    for i, res_key in enumerate(resources, start=2):
        resp = send_request(pipe, i, "market_trade", {"resource": res_key, "action": "buy", "units": 1})
        print(f"Resource {res_key:16s}: {resp.get('result')}")
