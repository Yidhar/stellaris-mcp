import json
import time

PIPE_PATH = r"\\.\pipe\stellaris_mcp_bridge"

def send_request(pipe, req_id, method, params=None):
    if params is None:
        params = {}
    payload = {
        "jsonrpc": "2.0",
        "id": req_id,
        "method": method,
        "params": params
    }
    msg = json.dumps(payload) + "\n"
    pipe.write(msg.encode('utf-8'))
    pipe.flush()
    line = pipe.readline().decode('utf-8').strip()
    return json.loads(line)

def main():
    with open(PIPE_PATH, "r+b", buffering=0) as pipe:
        print("=== 1. Testing set_job_priority (physicist) ===")
        res = send_request(pipe, 1, "set_job_priority", {"planet_id": 3, "job_key": "physicist"})
        print(f"[+] Priority set response: {json.dumps(res, indent=2)}")

        # Check jobs again
        res = send_request(pipe, 2, "get_planet_jobs", {"planet_id": 3})
        jobs_info = res.get("result", {})
        for s in jobs_info.get("strata", []):
            for j in s.get("jobs", []):
                if j.get("job_key") == "physicist":
                    print(f"[+] Physicist status after set_job_priority: is_prioritized={j.get('is_prioritized')}")

        # Check workforce_summary in get_planet_details
        res = send_request(pipe, 3, "get_planet_details", {"planet_id": 3})
        summary = res.get("result", {}).get("workforce_summary", {})
        print(f"[+] Planet workforce_summary prioritized_job: {summary.get('prioritized_job')}")

        print("\n=== 2. Toggling set_job_priority again (physicist) ===")
        res = send_request(pipe, 4, "set_job_priority", {"planet_id": 3, "job_key": "physicist"})
        print(f"[+] Priority toggle response: {json.dumps(res, indent=2)}")

        res = send_request(pipe, 5, "get_planet_jobs", {"planet_id": 3})
        jobs_info = res.get("result", {})
        for s in jobs_info.get("strata", []):
            for j in s.get("jobs", []):
                if j.get("job_key") == "physicist":
                    print(f"[+] Physicist status after toggle: is_prioritized={j.get('is_prioritized')}")

        print("\n=== 3. Testing set_job_workforce_limit (technician -> 1000) ===")
        res = send_request(pipe, 6, "set_job_workforce_limit", {"planet_id": 3, "job_key": "technician", "limit": 1000})
        print(f"[+] Limit set response: {json.dumps(res, indent=2)}")

        res = send_request(pipe, 7, "get_planet_jobs", {"planet_id": 3})
        jobs_info = res.get("result", {})
        for s in jobs_info.get("strata", []):
            for j in s.get("jobs", []):
                if j.get("job_key") == "technician":
                    print(f"[+] Technician status after limit: current={j.get('current_workforce')}, max={j.get('max_workforce')}, limit={j.get('workforce_limit')}")

        print("\n=== 4. Restoring technician limit (-1) ===")
        res = send_request(pipe, 8, "set_job_workforce_limit", {"planet_id": 3, "job_key": "technician", "limit": -1})
        print(f"[+] Limit restore response: {json.dumps(res, indent=2)}")

        res = send_request(pipe, 9, "get_planet_jobs", {"planet_id": 3})
        jobs_info = res.get("result", {})
        for s in jobs_info.get("strata", []):
            for j in s.get("jobs", []):
                if j.get("job_key") == "technician":
                    print(f"[+] Technician status after restore: current={j.get('current_workforce')}, max={j.get('max_workforce')}, limit={j.get('workforce_limit')}")

if __name__ == "__main__":
    main()
