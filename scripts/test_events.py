"""Test script for Stellaris Phase 2: Active Events and Option Selection via Named Pipe.
"""

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
    try:
        with open(PIPE_PATH, "r+b", buffering=0) as pipe:
            print("[+] Connected to Stellaris Bridge!")

            # 1. Get status
            print("\n--- 1. Game Status ---")
            resp = send_request(pipe, 1, "get_status")
            print("Status:", json.dumps(resp.get("result", {}), indent=2, ensure_ascii=False))

            # 2. Get active events
            print("\n--- 2. Querying Active Events ---")
            resp = send_request(pipe, 2, "get_active_events")
            result = resp.get("result", [])
            events = result if isinstance(result, list) else result.get("events", [])
            print(f"[+] Found {len(events)} active event window(s).")

            for idx, ev in enumerate(events):
                print(f"\n--- Event #{idx + 1} ---")
                print(f"  Window ID:   {ev.get('window_id')} (Hex: 0x{ev.get('window_id', 0):X})")
                print(f"  Title:       {ev.get('title')}")
                print(f"  Description: {ev.get('description')}")
                print("  Options:")
                for opt in ev.get("options", []):
                    print(f"    [{opt.get('index')}] (Valid: {opt.get('is_valid')}) {opt.get('text')}")

            if not events:
                print("\n[*] Currently no event windows are open in game.")
                return

            # If there's an event, test resolving the first one!
            target_ev = events[0]
            win_id = target_ev.get("window_id")
            print(f"\n--- 3. Testing resolve_event for Window ID {win_id} (Option 0) ---")
            resolve_resp = send_request(pipe, 3, "resolve_event", {
                "window_id": win_id,
                "option_index": 0
            })
            print("Resolve Response:", json.dumps(resolve_resp, indent=2, ensure_ascii=False))

            # Re-check active events
            print("\n--- 4. Querying Active Events After Resolution ---")
            time.sleep(0.5)
            resp2 = send_request(pipe, 4, "get_active_events")
            res2 = resp2.get("result", [])
            events2 = res2 if isinstance(res2, list) else res2.get("events", [])
            print(f"[+] Remaining active events: {len(events2)}")

    except FileNotFoundError:
        print(f"[-] Named Pipe not found at {PIPE_PATH}. Is stellaris.exe running with stellaris_bridge.dll?")
    except Exception as e:
        print(f"[-] Error: {e}")

if __name__ == "__main__":
    main()
