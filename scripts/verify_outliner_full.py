"""Full Pipeline Verification for Outliner Progressive Disclosure System.
Validates IPC and MCP layers for:
- Layer 1: stellaris_get_outliner
- Layer 2: stellaris_get_sectors
- Layer 2: stellaris_get_military_fleets
- Layer 2: stellaris_get_civilian_fleets
- Layer 2: stellaris_get_armies
- Layer 3: stellaris_get_planet_details
"""

import subprocess
import json
import sys

def test_mcp():
    proc = subprocess.Popen(
        ['node', 'd:/stellarismcp/stellaris_mcp_server/dist/index.js'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding='utf-8',
        bufsize=1
    )

    def rpc(msg):
        proc.stdin.write(json.dumps(msg) + '\n')
        proc.stdin.flush()
        line = proc.stdout.readline()
        return json.loads(line.strip())

    # Initialize
    init_res = rpc({
        'jsonrpc': '2.0',
        'id': 1,
        'method': 'initialize',
        'params': {
            'protocolVersion': '2024-11-05',
            'capabilities': {},
            'clientInfo': {'name': 'test-runner', 'version': '1.0'}
        }
    })
    proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n')
    proc.stdin.flush()

    tools_to_test = [
        ('stellaris_get_outliner', {}),
        ('stellaris_get_sectors', {}),
        ('stellaris_get_military_fleets', {}),
        ('stellaris_get_civilian_fleets', {}),
        ('stellaris_get_armies', {}),
        ('stellaris_get_planet_details', {'planet_id': 11})
    ]

    results = {}
    for name, args in tools_to_test:
        resp = rpc({'jsonrpc': '2.0', 'id': 2, 'method': 'tools/call', 'params': {'name': name, 'arguments': args}})
        if 'error' in resp:
            print(f"[FAIL] {name}: {resp['error']}")
            sys.exit(1)
        
        content = resp['result']['content'][0]['text']
        data = json.loads(content)
        results[name] = data
        print(f"[PASS] {name}: OK")

    proc.terminate()
    proc.wait()

    # Save validation output to file
    with open('d:/stellarismcp/scripts/outliner_verification_results.json', 'w', encoding='utf-8') as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    print("\nVerification results saved to scripts/outliner_verification_results.json")

if __name__ == '__main__':
    test_mcp()
