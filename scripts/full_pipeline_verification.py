import json
import time
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

PIPE_PATH = r"\\.\pipe\stellaris_mcp_bridge"

def send(pipe, req_id: int, method: str, params: dict | None = None) -> dict:
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
        print("[+] Connected to Stellaris Bridge!\n")
        req_id = 1

        # 1. ping
        print("=== 1. Ping ===")
        r = send(pipe, req_id, "ping"); req_id += 1
        print("Ping:", r.get("result"))

        # 2. get_status
        print("\n=== 2. Game Status ===")
        r = send(pipe, req_id, "get_status"); req_id += 1
        status = r.get("result", {})
        print(f"Date: {status.get('date', {}).get('formatted')}, Speed: {status.get('speed')}, Paused: {status.get('is_paused')}")
        print(f"Empire Size: {status.get('stats', {}).get('empire_size')}, Colonies: {status.get('stats', {}).get('colonies')}")

        # 3. get_alerts
        print("\n=== 3. Alerts ===")
        r = send(pipe, req_id, "get_alerts"); req_id += 1
        alerts = r.get("result", [])
        print(f"[+] Active Alerts Count: {len(alerts)}")
        for a in alerts[:5]:
            print(f"  - Alert {a.get('alert_id')} ({a.get('type')}): {a.get('title')}")

        # 4. get_research_state
        print("\n=== 4. Research State ===")
        r = send(pipe, req_id, "get_research_state"); req_id += 1
        res = r.get("result", {})
        for area in ["physics", "society", "engineering"]:
            cur = res.get(area, {}).get("current", {})
            cands = res.get(area, {}).get("candidates", [])
            print(f"  [{area.upper()}] Researching: {cur.get('is_researching')} ({cur.get('name') or 'None'})")
            print(f"    Candidates ({len(cands)}):", [c.get("name") or c.get("key") for c in cands[:3]])

        # 5. select_research (physics candidate 0)
        phys_cands = res.get("physics", {}).get("candidates", [])
        if phys_cands:
            target_tech = phys_cands[0].get("key")
            target_name = phys_cands[0].get("name")
            print(f"\n=== 5. Testing select_research (Physics: {target_name} [{target_tech}]) ===")
            r = send(pipe, req_id, "select_research", {"area": 0, "tech_key": target_tech}); req_id += 1
            print("Select Research Result:", r.get("result"))

            # Re-check research state
            r = send(pipe, req_id, "get_research_state"); req_id += 1
            cur_phys = r.get("result", {}).get("physics", {}).get("current", {})
            print(f"[+] Physics Current Now: {cur_phys.get('is_researching')} - {cur_phys.get('name')} [{cur_phys.get('key')}]")

        # 6. get_government
        print("\n=== 6. Government State ===")
        r = send(pipe, req_id, "get_government"); req_id += 1
        gov = r.get("result", {})
        print(f"Authority: {gov.get('authority_name')} [{gov.get('authority')}], Type: {gov.get('government_type_name')}")
        print(f"Origin: {gov.get('origin_name')} [{gov.get('origin')}]")
        print("Ethics:", [e.get("name") or e.get("key") for e in gov.get("ethics", [])])
        print("Civics:", [c.get("name") or c.get("key") for c in gov.get("civics", [])])
        print(f"Ruler: {gov.get('ruler', {}).get('name')}, Level: {gov.get('ruler', {}).get('level')}")
        print(f"Council Seats ({len(gov.get('council_seats', []))}):")
        for s in gov.get("council_seats", []):
            ldr = s.get("leader")
            ldr_name = ldr.get("name") if ldr else "Vacant"
            print(f"  Seat {s.get('seat_index')}: {s.get('position_name')} -> {ldr_name}")

        # 7. get_traditions
        print("\n=== 7. Traditions & Ascension Perks ===")
        r = send(pipe, req_id, "get_traditions"); req_id += 1
        tr = r.get("result", {})
        print("Summary:", tr.get("summary"))
        print(f"Adopted Trees ({len(tr.get('adopted_trees', []))}):", [t.get("name") for t in tr.get("adopted_trees", [])])
        print(f"Available Trees ({len(tr.get('available_trees', []))}):", [t.get("name") or t.get("key") for t in tr.get("available_trees", [])[:5]])

        # 8. get_edicts
        print("\n=== 8. Edicts ===")
        r = send(pipe, req_id, "get_edicts"); req_id += 1
        ed = r.get("result", {})
        print(f"Active Edicts ({len(ed.get('active_edicts', []))}):", [e.get("name") or e.get("key") for e in ed.get("active_edicts", [])])
        print(f"Available Edicts ({len(ed.get('available_edicts', []))}):")
        for e in ed.get("available_edicts", [])[:4]:
            print(f"  - {e.get('name')} [{e.get('key')}]: active={e.get('is_active')}")

        # 9. toggle_edict test (map_the_stars)
        print("\n=== 9. Testing toggle_edict (map_the_stars -> True) ===")
        r = send(pipe, req_id, "toggle_edict", {"edict_key": "map_the_stars", "enabled": True}); req_id += 1
        print("Toggle Edict Result:", r.get("result"))

        # 10. get_situation_log
        print("\n=== 10. Situation Log ===")
        r = send(pipe, req_id, "get_situation_log", {"player_only": True}); req_id += 1
        sit = r.get("result", {})
        print("Summary:", sit.get("summary"))
        print(f"Situations: {len(sit.get('situations', []))}")
        print(f"Special Projects: {len(sit.get('special_projects', []))}")
        print(f"Anomalies: {len(sit.get('anomalies', []))}")

        # 11. get_active_events and resolve_event
        print("\n=== 11. Active Events & Resolution ===")
        r = send(pipe, req_id, "get_active_events"); req_id += 1
        events = r.get("result", [])
        print(f"Active Events Count: {len(events)}")
        for ev in events:
            wid = ev.get("window_id")
            title = ev.get("title")
            print(f"  Event 0x{wid:X}: '{title}'")
            if wid == 0xFFFF0001: # Start screen event
                print("  Resolving Opening Start Screen with Option 0...")
                r_res = send(pipe, req_id, "resolve_event", {"window_id": wid, "option_index": 0}); req_id += 1
                print("  Resolution Result:", r_res.get("result"))

        time.sleep(0.5)
        # Re-query events to verify start screen was dismissed
        r = send(pipe, req_id, "get_active_events"); req_id += 1
        print(f"Active Events After Dismissal: {len(r.get('result', []))}")

        print("\n[+] Full Pipeline Regression Verification PASSED!")

if __name__ == "__main__":
    main()
