"""Monitor Stellaris game until a new event occurs, then print and optionally resolve it.
"""

import json
import time
import sys

sys.stdout.reconfigure(encoding="utf-8")

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

        # Unpause and set speed 3 to let time flow
        send_request(pipe, 1, "set_speed", {"speed": 4})
        send_request(pipe, 2, "set_paused", {"paused": False})

        req_id = 10
        print("[*] Monitoring for events at Speed 4... (Checking every 1s)")
        for i in range(25):
            req_id += 1
            status_resp = send_request(pipe, req_id, "get_status")
            status = status_resp.get("result", {})

            req_id += 1
            ev_resp = send_request(pipe, req_id, "get_active_events")
            res = ev_resp.get("result", [])
            events = res if isinstance(res, list) else res.get("events", [])

            print(f"[{i+1}/25] Paused: {status.get('is_paused')}, Speed: {status.get('speed')}, Active Events: {len(events)}")

            if events:
                print("\n=======================================================")
                print(f"[!] EVENT DETECTED! Total active: {len(events)}")
                for idx, ev in enumerate(events):
                    print(f"\n--- Event #{idx+1} ---")
                    print(f"  Window ID:   {ev.get('window_id')} (0x{ev.get('window_id', 0):X})")
                    print(f"  Title:       {ev.get('title')}")
                    print(f"  Description: {ev.get('description')}")
                    print("  Options:")
                    for opt in ev.get("options", []):
                        print(f"    [{opt.get('index')}] (Valid: {opt.get('is_valid')}) {opt.get('text')}")
                print("=======================================================\n")

                # Pause to examine
                send_request(pipe, 999, "set_paused", {"paused": True})
                return

            time.sleep(1.0)

        # Pause before exiting
        send_request(pipe, 999, "set_paused", {"paused": True})
        print("[*] No new events appeared in 25 seconds (Game paused).")

if __name__ == "__main__":
    main()
