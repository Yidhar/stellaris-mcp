import win32file
import json
import time

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

print("解除暂停，推进游戏 3 秒...")
call('set_paused', {'paused': False})
time.sleep(3.0)
call('set_paused', {'paused': True})
print("游戏已恢复暂停，检查队列实时进度：")

res = call('get_planet_details', {'planet_id': 11})['result']
for idx, item in enumerate(res['construction_queue']):
    print(f"  Item {idx+1}: {item['key']} ({item['item_name']}) - 进度: {item['progress']}/{item['total_days']} 天 ({item['progress_percent']:.1f}%), 剩余: {item['remaining_days']} 天")

win32file.CloseHandle(pipe)
