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
    print("=== 1. Test get_outliner (Layer 1) ===")
    res1 = send_request(pipe, 1, "get_outliner")
    print(json.dumps(res1.get("result", {}), indent=2, ensure_ascii=False))

    print("\n=== 2. Test get_sectors (Layer 2) ===")
    res2 = send_request(pipe, 2, "get_sectors")
    print(json.dumps(res2.get("result", {}), indent=2, ensure_ascii=False))

    print("\n=== 3. Test get_military_fleets (Layer 2) ===")
    res3 = send_request(pipe, 3, "get_military_fleets")
    print(json.dumps(res3.get("result", {}), indent=2, ensure_ascii=False))

    print("\n=== 4. Test get_civilian_fleets (Layer 2) ===")
    res4 = send_request(pipe, 4, "get_civilian_fleets")
    print(json.dumps(res4.get("result", {}), indent=2, ensure_ascii=False))

    print("\n=== 5. Test get_armies (Layer 2) ===")
    res5 = send_request(pipe, 5, "get_armies")
    print(json.dumps(res5.get("result", {}), indent=2, ensure_ascii=False))

    print("\n=== 6. Test get_planet_details (Layer 3: Earth id=11) ===")
    res6 = send_request(pipe, 6, "get_planet_details", {"planet_id": 11})
    print(json.dumps(res6.get("result", {}), indent=2, ensure_ascii=False))
