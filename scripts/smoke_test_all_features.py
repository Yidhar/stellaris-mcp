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

# All read-only query methods across the bridge
READ_METHODS = [
    ("ping", {}),
    ("get_status", {}),
    ("get_active_events", {}),
    ("get_notifications", {}),
    ("get_alerts", {}),
    ("get_research_state", {}),
    ("get_situation_log", {}),
    ("get_government", {}),
    ("get_traditions", {}),
    ("get_edicts", {}),
    ("get_leaders", {}),
    ("get_species", {}),
    ("get_species_modification_info", {}),
    ("get_fleets", {}),
    ("get_ship_designs", {}),
    ("get_ship_design_catalog", {}),
    ("get_market", {}),
    ("get_discoveries", {}),
    ("get_contacts", {}),
    ("get_outliner", {}),
    ("get_sectors", {}),
    ("get_military_fleets", {}),
    ("get_civilian_fleets", {}),
    ("get_armies", {}),
]

def main():
    print(f"[*] Connecting to Stellaris Bridge: {PIPE_PATH}")
    results = {}
    with open(PIPE_PATH, "r+b", buffering=0) as pipe:
        print("[+] Connected!\n")
        req_id = 1
        
        # 1. Run all general read methods
        for method, params in READ_METHODS:
            try:
                t0 = time.time()
                resp = send_request(pipe, req_id, method, params)
                dt = (time.time() - t0) * 1000
                req_id += 1
                if "error" in resp:
                    print(f"[-] {method:<30} FAIL in {dt:6.1f}ms: {resp['error']}")
                    results[method] = ("ERROR", resp["error"])
                else:
                    res = resp.get("result", {})
                    # summarize
                    summary = ""
                    if isinstance(res, dict):
                        keys = list(res.keys())
                        summary = f"keys: {keys[:5]} (total {len(keys)})"
                    elif isinstance(res, list):
                        summary = f"list count: {len(res)}"
                    else:
                        summary = str(res)[:40]
                    print(f"[+] {method:<30} OK   in {dt:6.1f}ms: {summary}")
                    results[method] = ("OK", dt)
            except Exception as e:
                print(f"[!] {method:<30} EXCEPTION: {e}")
                results[method] = ("EXCEPTION", str(e))

        # 2. Test planet-specific queries if planets exist in sectors
        print("\n--- Planet-specific Queries ---")
        sectors_resp = send_request(pipe, req_id, "get_sectors", {})
        req_id += 1
        planet_id = None
        if "result" in sectors_resp and "sectors" in sectors_resp["result"]:
            for sec in sectors_resp["result"]["sectors"]:
                if sec.get("colonies"):
                    planet_id = sec["colonies"][0].get("id")
                    break
        
        if planet_id is None:
            # fallback to planet 0 (Earth / Capital)
            planet_id = 0

        planet_methods = [
            ("get_planet_details", {"planet_id": planet_id}),
            ("get_available_district_zones", {"planet_id": planet_id}),
            ("get_buildable_buildings", {"planet_id": planet_id}),
            ("get_clearable_blockers", {"planet_id": planet_id}),
            ("get_planetary_decisions", {"planet_id": planet_id}),
            ("get_terraforming_options", {"planet_id": planet_id}),
            ("get_planetary_features", {"planet_id": planet_id}),
            ("get_planet_jobs", {"planet_id": planet_id}),
            ("get_planet_armies", {"planet_id": planet_id}),
        ]

        for method, params in planet_methods:
            try:
                t0 = time.time()
                resp = send_request(pipe, req_id, method, params)
                dt = (time.time() - t0) * 1000
                req_id += 1
                if "error" in resp:
                    print(f"[-] {method:<30} FAIL in {dt:6.1f}ms: {resp['error']}")
                    results[method] = ("ERROR", resp["error"])
                else:
                    res = resp.get("result", {})
                    summary = ""
                    if isinstance(res, dict):
                        keys = list(res.keys())
                        summary = f"keys: {keys[:5]} (total {len(keys)})"
                    elif isinstance(res, list):
                        summary = f"list count: {len(res)}"
                    else:
                        summary = str(res)[:40]
                    print(f"[+] {method:<30} OK   in {dt:6.1f}ms: {summary}")
                    results[method] = ("OK", dt)
            except Exception as e:
                print(f"[!] {method:<30} EXCEPTION: {e}")
                results[method] = ("EXCEPTION", str(e))

    # Summary
    ok_cnt = sum(1 for v in results.values() if v[0] == "OK")
    print(f"\n==================================================")
    print(f"Read Methods Audit Summary: {ok_cnt}/{len(results)} passed cleanly.")
    print(f"==================================================")

if __name__ == "__main__":
    main()
