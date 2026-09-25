import subprocess
import json
import time

def main():
    proc = subprocess.Popen(
        ['node', 'd:/stellarismcp/stellaris_mcp_server/dist/index.js'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding='utf-8'
    )

    req_id = 0
    def rpc(method, params):
        nonlocal req_id
        req_id += 1
        msg = {'jsonrpc': '2.0', 'id': req_id, 'method': method, 'params': params}
        proc.stdin.write(json.dumps(msg) + '\n')
        proc.stdin.flush()
        line = proc.stdout.readline()
        return json.loads(line.strip())

    rpc('initialize', {'protocolVersion': '2024-11-05', 'capabilities': {}, 'clientInfo': {'name': 'test', 'version': '1.0'}})
    proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n')
    proc.stdin.flush()

    status_res = rpc('tools/call', {'name': 'stellaris_get_status', 'arguments': {}})
    status = json.loads(status_res['result']['content'][0]['text'])
    print('Game status:', status)
    if not status.get('in_game'):
        print('[-] Game is not currently loaded in a save. Please enter a game save first.')
        proc.kill()
        return False

    # Get species
    sp_res = rpc('tools/call', {'name': 'stellaris_get_species', 'arguments': {'mode': 'empire'}})
    sp_text = json.loads(sp_res['result']['content'][0]['text'])
    founder_id = sp_text['founder_species_id']
    founder_name = sp_text['founder_species_name']
    print(f'[+] Founder species: ID {founder_id}, name: {founder_name}')

    # Test create_species_template WITHOUT name argument
    print('[*] Calling stellaris_create_species_template WITHOUT name argument...')
    create_res = rpc('tools/call', {
        'name': 'stellaris_create_species_template',
        'arguments': {
            'base_species_id': founder_id,
            'traits': ['trait_intelligent', 'trait_rapid_breeders', 'trait_unruly']
        }
    })
    create_text = json.loads(create_res['result']['content'][0]['text'])
    print('Create template result:', json.dumps(create_text, indent=2, ensure_ascii=False))

    assert create_text.get('success') == True, f"Creation failed: {create_text}"
    assert create_text.get('inherited_name') == True, "inherited_name should be True!"
    print(f"[+] Template successfully created with inherited name: {create_text.get('name')}")

    time.sleep(1.0)

    # Verify template in stellaris_get_species
    sp_res2 = rpc('tools/call', {'name': 'stellaris_get_species', 'arguments': {'mode': 'empire'}})
    sp_text2 = json.loads(sp_res2['result']['content'][0]['text'])
    templates = [s for s in sp_text2.get('species', []) if s.get('is_template')]
    print(f'[+] Found {len(templates)} templates:')
    for t in templates:
        print(f"  - ID {t['species_id']}: name='{t['name']}', plural='{t['plural']}', adj='{t['adjective']}'")

    # Clean up newest template
    if templates:
        newest = templates[-1]
        print(f"[*] Cleaning up created template {newest['species_id']}...")
        del_res = rpc('tools/call', {
            'name': 'stellaris_delete_species_template',
            'arguments': {'species_id': newest['species_id']}
        })
        print('Delete result:', del_res['result']['content'][0]['text'])

    print('[+] End-to-end verification completed successfully!')
    proc.kill()
    return True

if __name__ == '__main__':
    main()
