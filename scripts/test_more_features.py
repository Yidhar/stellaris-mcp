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
    # Test selling minerals
    res_sell = send_request(pipe, 1, "market_trade", {"resource": "minerals", "action": "sell", "units": 100})
    print("Sell result:", res_sell)

    # Test removing monthly trade
    res_cancel = send_request(pipe, 2, "set_monthly_trade", {"resource": "alloys", "action": "buy", "amount": 10, "cancel": True})
    print("Cancel monthly trade result:", res_cancel)

    # Test activating relic (should fail safely with clear error because player currently has 0 relics)
    res_relic = send_request(pipe, 3, "activate_relic", {"relic_key": "r_the_surveyor"})
    print("Relic activation result:", res_relic)
