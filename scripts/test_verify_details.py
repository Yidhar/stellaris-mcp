import sys; sys.path.append('scripts')
import json, test_ipc_get_component_details

with open(r'\\.\pipe\stellaris_mcp_bridge', 'r+b', buffering=0) as pipe:
    resp = test_ipc_get_component_details.send_request(pipe, 1, 'get_component_details', {'key': 'PLASMA_3'})
    res = resp.get('result', resp)
    print('Set Key:', res['set_key'])
    print('Localized Name:', res['localized_name'])
    for v in res['variants']:
        stats = v.get('weapon_stats', {})
        print(f"  {v['size']:8} {v['name']}: dmg={stats.get('min_damage')}-{stats.get('max_damage')}, range={stats.get('range')}, cd={stats.get('cooldown')}, pwr={v['power']}")
