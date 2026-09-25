import win32file
import json

pipe = win32file.CreateFile(
    r'\\.\pipe\stellaris_mcp_bridge',
    win32file.GENERIC_READ | win32file.GENERIC_WRITE,
    0, None, win32file.OPEN_EXISTING, 0, None
)

def call(method, params={}):
    req = {'jsonrpc': '2.0', 'id': 1, 'method': method, 'params': params}
    win32file.WriteFile(pipe, (json.dumps(req) + '\n').encode('utf-8'))
    resp_line = b''
    while not resp_line.endswith(b'\n'):
        hr, data = win32file.ReadFile(pipe, 4096)
        resp_line += data
    return json.loads(resp_line.decode('utf-8'))

tasks = [
    # 城市区划 - 核心槽位 (slot 0)
    {'desc': '城市区划-核心槽位 (slot 0)', 'key': 'building_autochthon_monument', 'dtype': 'district_city', 'slot': 0},
    # 城市区划 - 专精槽位 1 (slot 1)
    {'desc': '城市区划-专精槽位1 (slot 1)', 'key': 'building_engineering_facility_1', 'dtype': 'district_city', 'slot': 1},
    # 城市区划 - 专精槽位 2 (slot 2)
    {'desc': '城市区划-专精槽位2 (slot 2)', 'key': 'building_foundry_1', 'dtype': 'district_city', 'slot': 2},
    # 采矿区划 (slot 64)
    {'desc': '采矿区划-特化槽位 (slot 64)', 'key': 'building_mineral_purification_plant', 'dtype': 'district_mining', 'slot': 64},
    # 农业区划 (slot 65)
    {'desc': '农业区划-特化槽位 (slot 65)', 'key': 'building_food_processing_facility', 'dtype': 'district_farming', 'slot': 65},
]

print("=== 开始批量下发区划建筑建造指令 ===")
for t in tasks:
    res = call('build_building', {
        'planet_id': 11,
        'building_key': t['key'],
        'district_type': t['dtype'],
        'slot_index': t['slot']
    })
    print(f"[{t['desc']}] -> 建筑: {t['key']}")
    result = res.get('result', {})
    if result.get('success'):
        print(f"  [+] 成功入队! queue_id={result.get('queue_id')}, zone_id={result.get('zone_id')}")
    else:
        print(f"  [-] 失败: {res.get('error') or result.get('error')}")

print("\n=== 查询建造后的地表建造队列 (Queue 0) ===")
queue_res = call('get_planet_details', {'planet_id': 11}).get('result', {})
for idx, item in enumerate(queue_res.get('construction_queue', [])):
    print(f"  Item {idx+1}: {item['key']} ({item['item_name']}) - 进度: {item['progress']}/{item['total_days']} 天 ({item['progress_percent']:.1f}%), 剩余: {item['remaining_days']} 天")

win32file.CloseHandle(pipe)
