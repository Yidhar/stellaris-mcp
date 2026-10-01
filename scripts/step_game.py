import win32file, json, time

handle = win32file.CreateFile(
    r'\\.\pipe\stellaris_mcp_bridge',
    win32file.GENERIC_READ | win32file.GENERIC_WRITE,
    0, None, win32file.OPEN_EXISTING, 0, None
)

def call(method, params=None):
    req = {'jsonrpc': '2.0', 'id': 1, 'method': method}
    if params: req['params'] = params
    win32file.WriteFile(handle, (json.dumps(req) + '\n').encode('utf-8'))
    buf = b''
    while b'\n' not in buf:
        _, data = win32file.ReadFile(handle, 65536)
        buf += data
    return json.loads(buf.split(b'\n')[0].decode('utf-8'))

print('Unpausing game for 2 seconds to let ticks advance...')
call('set_paused', {'paused': False})
time.sleep(2.0)
call('set_paused', {'paused': True})
print('Paused again.')

res = call('get_planet_details', {'planet_id': 11})
queue = res.get('result', {}).get('construction_queue', [])
print('Construction queue length:', len(queue))
for item in queue:
    print('  ->', item)

districts = res.get('result', {}).get('districts', [])
print('Checking Earth districts and buildings:')
for d in districts:
    dtype = d['type']
    built = d['built']
    slots = d['zone_slots']
    print(f"[{dtype}] built={built}, zone slots={slots}")
    for z in d.get('zones', []):
        sid = z['slot_id']
        zkey = z['key']
        bldgs = z['buildings']
        print(f"   zone {sid} ({zkey}) has {len(bldgs)} buildings:")
        for b in bldgs:
            bkey = b['key']
            bname = b['name']
            print(f"      - {bkey} ({bname})")

win32file.CloseHandle(handle)
