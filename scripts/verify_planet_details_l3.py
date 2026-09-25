import json
import win32file
import sys

PIPE_NAME = r'\\.\pipe\stellaris_mcp_bridge'

def call_bridge(method, params=None):
    handle = win32file.CreateFile(
        PIPE_NAME,
        win32file.GENERIC_READ | win32file.GENERIC_WRITE,
        0, None,
        win32file.OPEN_EXISTING,
        0, None
    )
    req = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": method,
        "params": params or {}
    }
    payload = json.dumps(req) + "\n"
    win32file.WriteFile(handle, payload.encode('utf-8'))
    
    chunks = []
    while True:
        hr, data = win32file.ReadFile(handle, 65536)
        chunks.append(data)
        if len(data) < 65536:
            break
            
    win32file.CloseHandle(handle)
    resp = json.loads(b''.join(chunks).decode('utf-8'))
    return resp.get("result", resp)

def main():
    print("=" * 60)
    print("=== Testing Layer 3: get_planet_details (Planet ID 11: Earth) ===")
    print("=" * 60)
    res_earth = call_bridge("get_planet_details", {"planet_id": 11})
    print(json.dumps(res_earth, indent=2, ensure_ascii=False))

    print("\n" + "=" * 60)
    print("=== Testing Layer 3: get_planet_details (Planet ID 63: Khor-I Colonizing) ===")
    print("=" * 60)
    res_khor = call_bridge("get_planet_details", {"planet_id": 63})
    print(json.dumps(res_khor, indent=2, ensure_ascii=False))

if __name__ == '__main__':
    main()
