import json
import time
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
    print(f"[*] Connecting to Named Pipe: {PIPE_PATH}")
    with open(PIPE_PATH, "r+b", buffering=0) as pipe:
        print("[+] Connected to Stellaris Bridge!")

        # 1. Get status
        status = send_request(pipe, 1, "get_status")
        print("Status:", json.dumps(status, indent=2))

        # 2. Get clearable blockers for planet 3 (Earth)
        print("\n--- Testing get_clearable_blockers for planet_id 3 ---")
        blockers = send_request(pipe, 2, "get_clearable_blockers", {"planet_id": 3})
        print("Blockers result:", json.dumps(blockers, indent=2, ensure_ascii=False))

        # Check if we have clearable blockers
        res = blockers.get("result", {})
        blocker_list = res.get("blockers", [])
        target_dep = None
        for b in blocker_list:
            if b.get("can_clear"):
                target_dep = b
                break

        if not target_dep:
            print("No blocker available to clear (or already queued/cannot clear).")
            return

        print(f"\n--- Testing clear_blocker for deposit {target_dep.get('deposit_id')} ({target_dep.get('key')}) ---")
        clear_res = send_request(pipe, 3, "clear_blocker", {
            "planet_id": 3,
            "deposit_id": target_dep.get("deposit_id")
        })
        print("Clear blocker response:", json.dumps(clear_res, indent=2, ensure_ascii=False))

        # Check queue
        print("\n--- Checking planet details after clear command ---")
        planet_res = send_request(pipe, 4, "get_planet_details", {"planet_id": 3})
        queue = planet_res.get("result", {}).get("construction_queue", [])
        print("Construction Queue:", json.dumps(queue, indent=2, ensure_ascii=False))

        # Also re-check blockers
        print("\n--- Re-checking get_clearable_blockers ---")
        blockers2 = send_request(pipe, 5, "get_clearable_blockers", {"planet_id": 3})
        print("Blockers after queue:", json.dumps(blockers2, indent=2, ensure_ascii=False))

if __name__ == "__main__":
    main()
