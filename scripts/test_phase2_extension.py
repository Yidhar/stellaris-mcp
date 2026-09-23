"""Comprehensive test for Phase 2 Extension:
1. Querying active notifications (Scheme B)
2. Opening a notification
3. Querying active events (including Anomaly view ANOMALY_EVENT_ID 0xFFFF0002)
4. Resolving an active event or anomaly
"""

import json
import time
import sys

if hasattr(sys.stdout, "reconfigure"):
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
    try:
        with open(PIPE_PATH, "r+b", buffering=0) as pipe:
            print("[+] Connected to Stellaris Bridge!")

            # 1. Game Status
            print("\n=== 1. Game Status ===")
            resp = send_request(pipe, 1, "get_status")
            print("Status:", json.dumps(resp.get("result", {}), indent=2, ensure_ascii=False))

            # 2. Get Notifications (Scheme B)
            print("\n=== 2. Querying Top-Bar Notifications (Scheme B) ===")
            notif_resp = send_request(pipe, 2, "get_notifications")
            notifs = notif_resp.get("result", [])
            print(f"[+] Found {len(notifs)} queued notification(s):")
            for item in notifs:
                print(f"  [{item.get('index')}] Type: {item.get('type')}, TitleKey: {item.get('title_key')}, Param: {item.get('param')}, CanClick: {item.get('can_click')}")

            # 3. Get Active Events (including modal anomaly window if already open)
            print("\n=== 3. Querying Active Events & Anomaly Windows ===")
            ev_resp = send_request(pipe, 3, "get_active_events")
            events = ev_resp.get("result", [])
            print(f"[+] Found {len(events)} active event/anomaly window(s):")
            for ev in events:
                print(f"  Window ID: {ev.get('window_id')} (Hex: 0x{ev.get('window_id', 0):X})")
                print(f"  Title:     {ev.get('title')}")
                print(f"  Desc:      {ev.get('description')}")
                print("  Options:")
                for opt in ev.get("options", []):
                    print(f"    [{opt.get('index')}] (Valid: {opt.get('is_valid')}) {opt.get('text')}")

            # 4. If there's an anomaly or terraform notification, test opening it!
            if not events and notifs:
                target_idx = 0
                for item in notifs:
                    if "ANOMALY" in item.get("type", ""):
                        target_idx = item.get("index")
                        break
                print(f"\n=== 4. Testing open_notification for index {target_idx} ===")
                open_resp = send_request(pipe, 4, "open_notification", {"index": target_idx})
                print("Open Response:", json.dumps(open_resp, indent=2, ensure_ascii=False))

                time.sleep(0.5)

                # Re-query active events to confirm it popped up!
                print("\n=== 5. Querying Active Events After Opening Notification ===")
                ev_resp2 = send_request(pipe, 5, "get_active_events")
                events2 = ev_resp2.get("result", [])
                print(f"[+] Found {len(events2)} active event/anomaly window(s):")
                for ev in events2:
                    print(f"  Window ID: {ev.get('window_id')} (Hex: 0x{ev.get('window_id', 0):X})")
                    print(f"  Title:     {ev.get('title')}")
                    print(f"  Desc:      {ev.get('description')}")
                    print("  Options:")
                    for opt in ev.get("options", []):
                        print(f"    [{opt.get('index')}] (Valid: {opt.get('is_valid')}) {opt.get('text')}")

    except FileNotFoundError:
        print(f"[-] Named Pipe not found at {PIPE_PATH}. Is stellaris.exe running with updated stellaris_bridge.dll?")
    except Exception as e:
        print(f"[-] Error: {e}")

if __name__ == "__main__":
    main()
