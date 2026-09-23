import subprocess
import json

def test_mcp_server():
    proc = subprocess.Popen(
        ["node", "dist/index.js"],
        cwd=r"d:\stellarismcp\stellaris_mcp_server",
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        bufsize=0
    )

    def send_rpc(msg):
        line = json.dumps(msg) + "\n"
        proc.stdin.write(line)
        proc.stdin.flush()
        resp_line = proc.stdout.readline()
        return json.loads(resp_line)

    # 1. Initialize
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
    init_resp = send_rpc(init_req)
    print("Initialize Response:", init_resp.get("result", {}).get("serverInfo"))

    # 2. Call stellaris_get_fleets
    call_req = {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/call",
        "params": {
            "name": "stellaris_get_fleets",
            "arguments": {
                "include_civilian": False
            }
        }
    }
    call_resp = send_rpc(call_req)
    content = call_resp.get("result", {}).get("content", [{}])[0].get("text", "{}")
    data = json.loads(content)
    print("stellaris_get_fleets Result:")
    print(json.dumps(data, indent=2, ensure_ascii=False))

    proc.terminate()
    proc.wait()

if __name__ == "__main__":
    test_mcp_server()
