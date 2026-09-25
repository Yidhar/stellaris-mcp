import subprocess
import json
import time

def main():
    cmd = ["node", "d:/stellarismcp/stellaris_mcp_server/dist/index.js"]
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        bufsize=1
    )

    def send_rpc(msg: dict) -> dict:
        line = json.dumps(msg) + "\n"
        proc.stdin.write(line)
        proc.stdin.flush()
        resp_line = proc.stdout.readline()
        return json.loads(resp_line.strip())

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
    resp = send_rpc(init_req)
    proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
    proc.stdin.flush()

    # 2. tools/list
    list_req = {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}}
    resp = send_rpc(list_req)
    tools = resp.get("result", {}).get("tools", [])
    print(f"Total tools registered: {len(tools)}")
    tool_names = [t["name"] for t in tools]
    for expected in [
        "stellaris_get_status",
        "stellaris_get_market",
        "stellaris_market_trade",
        "stellaris_set_monthly_trade",
        "stellaris_get_discoveries",
        "stellaris_activate_relic",
        "stellaris_get_contacts"
    ]:
        print(f" - {expected} present: {expected in tool_names}")

    # 3. Call stellaris_get_status
    print("\n--- Testing MCP Tool: stellaris_get_status ---")
    c_req = {
        "jsonrpc": "2.0",
        "id": 3,
        "method": "tools/call",
        "params": {"name": "stellaris_get_status", "arguments": {}}
    }
    c_resp = send_rpc(c_req)
    txt = json.loads(c_resp["result"]["content"][0]["text"])
    print("Market:", txt.get("market"))
    print("Discoveries:", txt.get("discoveries"))
    print("Contacts:", txt.get("contacts"))

    # 4. Call stellaris_get_market
    print("\n--- Testing MCP Tool: stellaris_get_market ---")
    c_req = {
        "jsonrpc": "2.0",
        "id": 4,
        "method": "tools/call",
        "params": {"name": "stellaris_get_market", "arguments": {}}
    }
    c_resp = send_rpc(c_req)
    txt = json.loads(c_resp["result"]["content"][0]["text"])
    print("Tradable count:", txt.get("tradable_resources_count"))
    print("First 3 resources:", [r["key"] for r in txt.get("resources", [])[:3]])

    # 5. Call stellaris_get_discoveries
    print("\n--- Testing MCP Tool: stellaris_get_discoveries ---")
    c_req = {
        "jsonrpc": "2.0",
        "id": 5,
        "method": "tools/call",
        "params": {"name": "stellaris_get_discoveries", "arguments": {"tab": "all"}}
    }
    c_resp = send_rpc(c_req)
    txt = json.loads(c_resp["result"]["content"][0]["text"])
    print("Relics held:", txt.get("relics", {}).get("held_count"))
    print("Astral actions:", txt.get("astral_actions", {}).get("total_actions"))
    print("Artifact actions:", txt.get("artifact_actions", {}).get("total_actions"))

    # 6. Call stellaris_get_contacts
    print("\n--- Testing MCP Tool: stellaris_get_contacts ---")
    c_req = {
        "jsonrpc": "2.0",
        "id": 6,
        "method": "tools/call",
        "params": {"name": "stellaris_get_contacts", "arguments": {"mode": "all"}}
    }
    c_resp = send_rpc(c_req)
    txt = json.loads(c_resp["result"]["content"][0]["text"])
    print("Known empires count:", txt.get("total_known_empires"))
    print("Pending first contacts:", txt.get("pending_first_contacts_count"))

    # 7. Call stellaris_market_trade
    print("\n--- Testing MCP Tool: stellaris_market_trade ---")
    c_req = {
        "jsonrpc": "2.0",
        "id": 7,
        "method": "tools/call",
        "params": {"name": "stellaris_market_trade", "arguments": {"resource": "food", "action": "buy", "units": 100}}
    }
    c_resp = send_rpc(c_req)
    print("Trade result:", c_resp["result"]["content"][0]["text"])

    # 8. Call stellaris_set_monthly_trade
    print("\n--- Testing MCP Tool: stellaris_set_monthly_trade ---")
    c_req = {
        "jsonrpc": "2.0",
        "id": 8,
        "method": "tools/call",
        "params": {"name": "stellaris_set_monthly_trade", "arguments": {"resource": "consumer_goods", "action": "buy", "amount": 25, "price_limit": 50.0}}
    }
    c_resp = send_rpc(c_req)
    print("Monthly trade result:", c_resp["result"]["content"][0]["text"])

    # 9. Cancel monthly trade
    c_req = {
        "jsonrpc": "2.0",
        "id": 9,
        "method": "tools/call",
        "params": {"name": "stellaris_set_monthly_trade", "arguments": {"resource": "consumer_goods", "action": "buy", "amount": 25, "cancel": True}}
    }
    c_resp = send_rpc(c_req)
    print("Cancel monthly trade result:", c_resp["result"]["content"][0]["text"])

    proc.terminate()
    print("\n[+] All MCP tool end-to-end tests passed!")

if __name__ == "__main__":
    main()
