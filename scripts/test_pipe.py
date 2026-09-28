"""Test client for Stellaris Named Pipe IPC bridge.
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

    while True:
        resp_line = pipe.readline().decode("utf-8").strip()
        if not resp_line:
            continue
        data = json.loads(resp_line)
        if data.get("id") == req_id:
            return data

def main():
    print(f"[*] Connecting to Named Pipe: {PIPE_PATH}")
    try:
        with open(PIPE_PATH, "r+b", buffering=0) as pipe:
            print("[+] Connected to Stellaris Bridge!")

            # 1. Ping
            print("\n--- 1. Testing ping ---")
            resp = send_request(pipe, 1, "ping")
            print("Response:", json.dumps(resp, indent=2))

            # 2. Get status
            print("\n--- 2. Testing get_status ---")
            resp = send_request(pipe, 2, "get_status")
            print("Response:", json.dumps(resp, indent=2))

            # 3. Pause
            print("\n--- 3. Testing set_paused (pause) ---")
            resp = send_request(pipe, 3, "set_paused", {"paused": True})
            print("Response:", json.dumps(resp, indent=2))

            # Check status again
            resp = send_request(pipe, 4, "get_status")
            print("Status after pause:", json.dumps(resp, indent=2))

            # 4. Set speed to 3
            print("\n--- 4. Testing set_speed (3) ---")
            resp = send_request(pipe, 5, "set_speed", {"speed": 3})
            print("Response:", json.dumps(resp, indent=2))

            # Check status again
            resp = send_request(pipe, 6, "get_status")
            print("Status after speed change:", json.dumps(resp, indent=2))

            # 5. Unpause
            print("\n--- 5. Testing set_paused (unpause) ---")
            resp = send_request(pipe, 7, "set_paused", {"paused": False})
            print("Response:", json.dumps(resp, indent=2))

            print("\n[+] All pipe tests completed.")
    except FileNotFoundError:
        print(f"[-] Pipe {PIPE_PATH} not found. Ensure DLL is injected into stellaris.exe.")
        sys.exit(1)
    except Exception as e:
        print(f"[-] Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
