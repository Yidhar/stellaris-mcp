import sys, json, time
sys.stdout.reconfigure(encoding='utf-8')

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

# Wait a brief moment for pipe to be ready
time.sleep(0.5)

with open(PIPE_PATH, "r+b", buffering=0) as pipe:
    print("=== 1. Test get_sectors (sector_id=0, Core Sector) ===")
    r0 = send_request(pipe, 1, "get_sectors", {"sector_id": 0})
    print(json.dumps(r0.get("result", {}), indent=2, ensure_ascii=False))

    print("\n=== 2. Test get_sectors (sector_id=1, Frontier Sector) ===")
    r1 = send_request(pipe, 2, "get_sectors", {"sector_id": 1})
    print(json.dumps(r1.get("result", {}), indent=2, ensure_ascii=False))

    print("\n=== 3. Test get_sectors (all sectors, sector_id omitted) ===")
    r_all = send_request(pipe, 3, "get_sectors")
    print(json.dumps(r_all.get("result", {}), indent=2, ensure_ascii=False))
