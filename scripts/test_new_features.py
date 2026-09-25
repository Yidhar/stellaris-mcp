import json
import time

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

with open(PIPE_PATH, "r+b", buffering=0) as pipe:
    print("--- 1. Testing get_status (Summary indicators) ---")
    res_status = send_request(pipe, 1, "get_status")
    r = res_status.get("result", {})
    print("Market summary:", r.get("market"))
    print("Discoveries summary:", r.get("discoveries"))
    print("Contacts summary:", r.get("contacts"))

    print("\n--- 2. Testing get_market ---")
    res_market = send_request(pipe, 2, "get_market")
    rm = res_market.get("result", {})
    print(f"Tradable resources: {rm.get('tradable_resources_count')}, Market fee: {rm.get('market_fee_percent')}%")
    for res in rm.get("resources", []):
        print(f" - {res['key']} ({res['localized_name']}): stockpile={res['stockpile']}, unit_price={res['unit_base_price']}, buy={res['buy_price_per_unit']}, sell={res['sell_price_per_unit']}")

    print("\n--- 3. Testing get_discoveries ---")
    res_disc = send_request(pipe, 3, "get_discoveries", {"tab": "all"})
    rd = res_disc.get("result", {})
    relics = rd.get("relics", {})
    print(f"Relics held: {relics.get('held_count')}, Total galaxy relics: {relics.get('total_galaxy_relics')}")
    astral = rd.get("astral_actions", {})
    print(f"Astral actions count: {astral.get('total_actions')}")
    for act in astral.get("actions", [])[:3]:
        print(f" * Astral: {act['name']} ({act['key']})")
    art = rd.get("artifact_actions", {})
    print(f"Artifact actions count: {art.get('total_actions')}")
    for act in art.get("actions", [])[:3]:
        print(f" * Artifact: {act['name']} ({act['key']})")

    print("\n--- 4. Testing get_contacts ---")
    res_contacts = send_request(pipe, 4, "get_contacts", {"mode": "all"})
    rc = res_contacts.get("result", {})
    print(f"Total known empires: {rc.get('total_known_empires')}, Pending first contacts: {rc.get('pending_first_contacts_count')}")
    for emp in rc.get("empires", []):
        print(f" > Empire: {emp['name']} (ID {emp['country_id']}, type={emp['country_type']}), treaties={emp.get('treaties')}")
    for fc in rc.get("first_contacts", [])[:3]:
        print(f" > First Contact: {fc['name']} (ID {fc['country_id']}) - {fc.get('status')}")

    print("\n--- 5. Testing market_trade (buy 100 minerals) ---")
    res_trade = send_request(pipe, 5, "market_trade", {"resource": "minerals", "action": "buy", "units": 100})
    print("Trade result:", res_trade)

    print("\n--- 6. Testing set_monthly_trade (buy 10 alloys monthly) ---")
    res_mtrade = send_request(pipe, 6, "set_monthly_trade", {"resource": "alloys", "action": "buy", "amount": 10, "price_limit": 100.0})
    print("Monthly trade result:", res_mtrade)
