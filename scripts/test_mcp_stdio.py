"""Test MCP Server over stdio protocol (simulating an MCP Client like Antigravity / Claude).
"""

import subprocess
import json
import sys

def main():
    # the server to test: an argument (e.g. an installed plugin's mcp-server\dist\index.js), else the repo build
    cmd = ["node", sys.argv[1] if len(sys.argv) > 1 else "d:/stellarismcp/stellaris_mcp_server/dist/index.js"]
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",  # the server writes UTF-8 JSON (tool descriptions hold non-ASCII text)
        bufsize=1
    )

    def send_rpc(msg: dict) -> dict:
        line = json.dumps(msg) + "\n"
        proc.stdin.write(line)
        proc.stdin.flush()
        resp_line = proc.stdout.readline()
        return json.loads(resp_line.strip())

    print("=== 1. Testing MCP Initialize ===")
    init_req = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "test-client", "version": "1.0"}
        }
    }
    resp = send_rpc(init_req)
    print("Initialize Response:", json.dumps(resp, indent=2))

    # Notifications initialized
    proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
    proc.stdin.flush()

    print("\n=== 2. Testing tools/list ===")
    list_req = {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/list",
        "params": {}
    }
    resp = send_rpc(list_req)
    print("Tools List Response:", json.dumps(resp, indent=2))

    print("\n=== 3. Testing tools/call (stellaris_get_status) ===")
    call_req = {
        "jsonrpc": "2.0",
        "id": 3,
        "method": "tools/call",
        "params": {
            "name": "stellaris_get_status",
            "arguments": {}
        }
    }
    resp = send_rpc(call_req)
    print("Call Tool Response:", json.dumps(resp, indent=2))

    proc.terminate()
    proc.wait()

if __name__ == "__main__":
    main()
