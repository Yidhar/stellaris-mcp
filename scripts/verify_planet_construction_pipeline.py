import win32file, win32pipe, pywintypes, json, time

class BridgeClient:
    def __init__(self):
        self.handle = None
        self._connect()

    def _connect(self):
        pipe_name = r'\\.\pipe\stellaris_mcp_bridge'
        for attempt in range(10):
            try:
                self.handle = win32file.CreateFile(
                    pipe_name,
                    win32file.GENERIC_READ | win32file.GENERIC_WRITE,
                    0, None, win32file.OPEN_EXISTING, 0, None
                )
                return
            except pywintypes.error as e:
                if e.winerror == 231: # PIPE_BUSY
                    time.sleep(0.1)
                else:
                    time.sleep(0.2)
        raise RuntimeError("Failed to connect to bridge named pipe after 10 attempts")

    def call(self, method, params=None):
        req = {'jsonrpc': '2.0', 'id': int(time.time() * 1000) % 100000, 'method': method}
        if params:
            req['params'] = params
        payload = (json.dumps(req) + '\n').encode('utf-8')
        win32file.WriteFile(self.handle, payload)
        
        # Read until newline
        buf = b""
        while b'\n' not in buf:
            _, data = win32file.ReadFile(self.handle, 65536)
            buf += data
        line = buf.split(b'\n')[0]
        return json.loads(line.decode('utf-8', errors='ignore'))

    def close(self):
        if self.handle:
            win32file.CloseHandle(self.handle)
            self.handle = None

if __name__ == '__main__':
    client = BridgeClient()
    try:
        print("=== 1. Testing get_planet_details for Earth (11) ===")
        res_details = client.call('get_planet_details', {'planet_id': 11})
        print("Result success:", 'error' not in res_details)
        districts = res_details.get('result', {}).get('districts', [])
        print(f"Total dynamic districts retrieved: {len(districts)}")
        for d in districts:
            print(f"District [{d.get('type')}] built: {d.get('built')}/{d.get('max_capacity')}, zones: {len(d.get('zones', []))}")
            for z in d.get('zones', []):
                print(f"  -> Zone [{z.get('slot_id')} / {z.get('key')}]: {len(z.get('buildings', []))} buildings built")
                for b in z.get('buildings', []):
                    print(f"     - {b.get('key')} ({b.get('name')})")

        print("\n=== 2. Testing get_available_district_zones for Earth (11) ===")
        res_zones = client.call('get_available_district_zones', {'planet_id': 11, 'district_type': 'district_city'})
        print("City slots:")
        for s in res_zones.get('result', {}).get('slots', []):
            print(f"  Slot: {s.get('slot_id')} ({s.get('name')}), locked: {s.get('is_locked')}, available count: {len(s.get('available_zones', []))}")
            for z in s.get('available_zones', [])[:3]:
                print(f"    * {z.get('key')}: {z.get('name')}")

        print("\n=== 3. Testing get_buildable_buildings ===")
        for dtype, slot in [('district_generator', 63), ('district_mining', 64), ('district_farming', 65), ('district_city', 0), ('district_city', 1), ('district_city', 2)]:
            res_b = client.call('get_buildable_buildings', {'planet_id': 11, 'district_type': dtype, 'slot_index': slot})
            bldgs = res_b.get('result', {}).get('buildable_buildings', [])
            print(f"[{dtype} / slot {slot}] buildable buildings ({len(bldgs)}):")
            for b in bldgs:
                print(f"  - {b.get('key')}: {b.get('name')}")

        print("\n=== 4. Executing construction of 6 buildings on Earth (11) ===")
        targets = [
            # 1. Generator district
            {"planet_id": 11, "building_key": "building_energy_grid", "district_type": "district_generator", "slot_index": 63},
            # 2. Mining district
            {"planet_id": 11, "building_key": "building_mineral_purification_plant", "district_type": "district_mining", "slot_index": 64},
            # 3. Farming district
            {"planet_id": 11, "building_key": "building_food_processing_facility", "district_type": "district_farming", "slot_index": 65},
            # 4. City district category 1 (Municipal core)
            {"planet_id": 11, "building_key": "building_autochthon_monument", "district_type": "district_city", "slot_index": 0},
            # 5. City district category 2 (Research/Unity)
            {"planet_id": 11, "building_key": "building_biolab_1", "district_type": "district_city", "slot_index": 1},
            # 6. City district category 3 (Industrial)
            {"planet_id": 11, "building_key": "building_foundry_1", "district_type": "district_city", "slot_index": 2}
        ]

        for t in targets:
            res = client.call('build_building', t)
            print(f"Building {t['building_key']} -> {res}")
            time.sleep(0.05)

        print("\n=== 5. Checking updated Construction Queue on Earth (11) ===")
        res_updated = client.call('get_planet_details', {'planet_id': 11})
        queue = res_updated.get('result', {}).get('construction_queue', [])
        print(f"Total items in Earth construction queue: {len(queue)}")
        for item in queue:
            print(f"  -> [{item.get('type')}] {item.get('key')} ({item.get('item_name')}): {item.get('remaining_days')} days remaining ({item.get('progress_percent')}%)")

    finally:
        client.close()
