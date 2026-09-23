import json
import time

PIPE_PATH = r"\\.\pipe\stellaris_mcp_bridge"

def req(pipe, rid, method, params=None):
    msg = {'jsonrpc': '2.0', 'method': method, 'params': params or {}, 'id': rid}
    pipe.write((json.dumps(msg) + '\n').encode('utf-8'))
    pipe.flush()
    return json.loads(pipe.readline().decode('utf-8').strip())

with open(PIPE_PATH, 'r+b', buffering=0) as pipe:
    # 1. Query pool
    l_resp = req(pipe, 1, 'get_leaders')
    pool = l_resp.get('result', {}).get('pool_candidates', [])
    officials = [c for c in pool if c.get('class') == 'official']
    if not officials:
        print('[-] No official candidates found in pool')
        exit(1)
    
    cand = officials[0]
    cid = cand['id']
    print(f'[*] Testing with candidate ID {cid} ({cand.get("name")})...')

    # 2. Hire candidate
    h_resp = req(pipe, 2, 'hire_leader', {'candidate_id': cid})
    print('Hire response:', h_resp)
    time.sleep(0.5)

    # 3. Check get_leaders before assign
    l_resp = req(pipe, 3, 'get_leaders')
    hired = l_resp['result']['hired_leaders']
    l_before = [l for l in hired if l['id'] == cid][0]
    print(f'Leader {cid} before assign: type={l_before["assignment_type"]}, target={l_before["target_id"]}')

    # 4. Assign candidate as governor of planet 0
    a_resp = req(pipe, 4, 'assign_leader', {'leader_id': cid, 'assignment_type': 1, 'target_id': 0})
    print('Assign response:', a_resp)
    time.sleep(0.5)

    # 5. Check get_leaders after assign
    l_resp2 = req(pipe, 5, 'get_leaders')
    hired2 = l_resp2['result']['hired_leaders']
    l_after = [l for l in hired2 if l['id'] == cid][0]
    print(f'Leader {cid} after assign: type={l_after["assignment_type"]}, target={l_after["target_id"]}')

    # 6. Re-assign leader 93 back to planet 0
    a_resp2 = req(pipe, 6, 'assign_leader', {'leader_id': 93, 'assignment_type': 1, 'target_id': 0})
    print('Reassign 93 back response:', a_resp2)
    time.sleep(0.5)

    # 7. Dismiss test leader
    d_resp = req(pipe, 7, 'dismiss_leader', {'leader_id': cid})
    print(f'Dismiss {cid} response:', d_resp)
    time.sleep(0.5)

    # 8. Final verification
    l_resp3 = req(pipe, 8, 'get_leaders')
    final_hired = l_resp3['result']['hired_leaders']
    l93 = [l for l in final_hired if l['id'] == 93][0]
    print(f'Leader 93 final status: type={l93["assignment_type"]}, target={l93["target_id"]}')
    print(f'Total hired leaders now: {len(final_hired)}')
