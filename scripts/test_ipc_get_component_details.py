import json
import sys

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

def main():
    queries = [
        "PERDITION_BEAM_ION",
        "KINETIC_ARTILLERY_2",
        "ENERGY_TORPEDO_1",
        "SWARM_MISSILE_1",
        "PLASMA_3"
    ]

    with open(PIPE_PATH, "r+b", buffering=0) as pipe:
        req_id = 1
        for q in queries:
            resp = send_request(pipe, req_id, "get_component_details", {"key": q})
            req_id += 1
            res = resp.get("result", resp)
            print(f"\n================ QUERY: {q} ================")
            print(json.dumps(res, indent=2, ensure_ascii=False))

if __name__ == "__main__":
    main()
