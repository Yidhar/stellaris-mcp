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

            # 1. Test get_status (Layer 1)
            print("\n=== [Layer 1] Testing get_status ===")
            resp = send_request(pipe, 1, "get_status")
            status = resp.get("result", {})
            leaders_summary = status.get("leaders", {})
            print("Leaders summary in get_status:")
            print(json.dumps(leaders_summary, indent=2, ensure_ascii=False))

            # 2. Test get_leaders (Layer 2)
            print("\n=== [Layer 2] Testing get_leaders ===")
            resp = send_request(pipe, 2, "get_leaders")
            leaders_data = resp.get("result", {})
            
            hired = leaders_data.get("hired_leaders", [])
            pool = leaders_data.get("pool_candidates", [])
            print(f"Hired leaders count: {len(hired)}")
            for l in hired:
                print(f"  - ID: {l.get('id')}, Name: {l.get('name')}, Class: {l.get('class')}, Subclass: {l.get('subclass')}, "
                      f"Level: {l.get('level')}, Age: {l.get('age')}, Ethic: {l.get('ethic_name')}, "
                      f"Assignment: {l.get('assignment_type')} (Target: {l.get('target_id')})")

            print(f"\nPool candidates count: {len(pool)}")
            for c in pool[:6]:
                print(f"  - ID: {c.get('id')}, Name: {c.get('name')}, Class: {c.get('class')}, "
                      f"Level: {c.get('level')}, Age: {c.get('age')}, Ethic: {c.get('ethic_name')}, Cost: {c.get('hire_cost')}")

            if not pool:
                print("[-] No pool candidates found to test hire!")
                return

            candidate_to_hire = pool[0]
            cid = candidate_to_hire["id"]
            cname = candidate_to_hire["name"]
            print(f"\n=== [Layer 3] Testing hire_leader on candidate {cid} ({cname}) ===")
            hire_resp = send_request(pipe, 3, "hire_leader", {"candidate_id": cid})
            print("hire_leader response:", json.dumps(hire_resp, indent=2, ensure_ascii=False))

            time.sleep(0.5)

            # Check get_leaders again
            resp = send_request(pipe, 4, "get_leaders")
            updated_hired = resp.get("result", {}).get("hired_leaders", [])
            print(f"Hired leaders count after hire: {len(updated_hired)}")
            hired_ids = [l["id"] for l in updated_hired]
            if cid in hired_ids:
                print(f"[+] Candidate {cid} is now in hired_leaders!")
            else:
                print(f"[-] Candidate {cid} NOT found in hired_leaders!")

            # 3. Test dismiss_leader
            print(f"\n=== [Layer 3] Testing dismiss_leader on newly hired leader {cid} ===")
            dismiss_resp = send_request(pipe, 5, "dismiss_leader", {"leader_id": cid})
            print("dismiss_leader response:", json.dumps(dismiss_resp, indent=2, ensure_ascii=False))

            time.sleep(0.5)

            # Check get_leaders again
            resp = send_request(pipe, 6, "get_leaders")
            final_hired = resp.get("result", {}).get("hired_leaders", [])
            print(f"Hired leaders count after dismiss: {len(final_hired)}")
            final_ids = [l["id"] for l in final_hired]
            if cid not in final_ids:
                print(f"[+] Leader {cid} successfully removed from hired_leaders!")
            else:
                print(f"[-] Leader {cid} still present in hired_leaders!")

            print("\n[+] Verification pipeline complete!")

    except Exception as e:
        print(f"[-] Error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()
