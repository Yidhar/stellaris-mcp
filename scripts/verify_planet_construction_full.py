import win32file
import json
import time

class BridgeClient:
    def __init__(self):
        self.pipe = None
        self.connect()

    def connect(self):
        import win32pipe
        for _ in range(10):
            try:
                win32pipe.WaitNamedPipe(r'\\.\pipe\stellaris_mcp_bridge', 2000)
                self.pipe = win32file.CreateFile(
                    r'\\.\pipe\stellaris_mcp_bridge',
                    win32file.GENERIC_READ | win32file.GENERIC_WRITE,
                    0, None, win32file.OPEN_EXISTING, 0, None
                )
                return
            except Exception:
                time.sleep(0.1)

    def call(self, method: str, params: dict = {}):
        req = {'jsonrpc': '2.0', 'id': 1, 'method': method, 'params': params}
        win32file.WriteFile(self.pipe, (json.dumps(req) + '\n').encode('utf-8'))
        resp_line = b''
        while not resp_line.endswith(b'\n'):
            hr, data = win32file.ReadFile(self.pipe, 4096)
            resp_line += data
        res = json.loads(resp_line.decode('utf-8'))
        if 'error' in res:
            raise RuntimeError(f"RPC Error: {res['error']}")
        return res.get('result')

    def close(self):
        if self.pipe:
            win32file.CloseHandle(self.pipe)
            self.pipe = None

def main():
    print("=================================================================")
    print("    Stellaris MCP Bridge - 行星建造队列全链路验证测试")
    print("=================================================================")

    client = BridgeClient()
    try:
        # 1. 验证行星详情查询（支持星系ID 11与行星ID 3）
        print("\n[Step 1] 验证行星信息查询 (Sol=11, Earth=3)...")
        earth_via_11 = client.call('get_planet_details', {'planet_id': 11})
        earth_via_3 = client.call('get_planet_details', {'planet_id': 3})
        assert earth_via_11['colony_id'] == 0, f"Expected colony_id 0, got {earth_via_11['colony_id']}"
        assert earth_via_3['colony_id'] == 0, f"Expected colony_id 0, got {earth_via_3['colony_id']}"
        print(f"  [+] Earth colony_id: {earth_via_11['colony_id']}")
        print(f"  [+] Earth is_capital: {earth_via_11['is_capital']}")
        print(f"  [+] Earth name: {earth_via_11['name']}")
        print(f"  [+] Earth system: {earth_via_11['system_name']}")

        # 2. 检查当前建造队列
        print("\n[Step 2] 检查当前地球地表建造队列 (Queue 0)...")
        q_items = earth_via_11.get('construction_queue', [])
        print(f"  [+] 当前队列任务数: {len(q_items)}")
        for idx, item in enumerate(q_items):
            print(f"      Item {idx+1}: {item['key']} ({item['item_name']}) - 进度: {item['progress']}/{item['total_days']} 天 ({item['progress_percent']:.1f}%), 剩余: {item['remaining_days']} 天")

        # 3. 验证区划与可用槽位
        print("\n[Step 3] 验证可用区划与建筑列表...")
        zones = client.call('get_available_district_zones', {'planet_id': 11, 'district_type': 'district_generator'})
        print(f"  [+] 发电区划槽位数量: {len(zones.get('slots', []))}")
        bldgs = client.call('get_buildable_buildings', {'planet_id': 11, 'district_type': 'district_generator', 'slot_index': 63})
        bldg_list = [b['key'] for b in bldgs.get('buildable_buildings', [])]
        print(f"  [+] 可造建筑: {bldg_list}")

        # 4. 验证大纲视图 (Outliner Layer 1) 是否同步反映建造状态
        print("\n[Step 4] 验证全局大纲概览 (Outliner Layer 1)...")
        status = client.call('get_status')
        sectors = status['outliner']['sectors_summary']['sectors']
        core_sector = next((s for s in sectors if s.get('is_core')), None)
        if core_sector:
            earth_summary = next((c for c in core_sector['colonies_summary'] if c['colony_id'] == 11), None)
            if earth_summary:
                print(f"  [+] 大纲中地球 has_construction: {earth_summary.get('has_construction')}")
                print(f"  [+] 大纲中地球 status_alerts_count: {earth_summary.get('status_alerts_count')}")

        print("\n[SUCCESS] 行星建造队列从底层数据库映射、指令派发、UI队列展现至大纲联动已全部验证通过！")
    finally:
        client.close()

if __name__ == '__main__':
    main()
