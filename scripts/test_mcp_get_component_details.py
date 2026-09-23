import subprocess, json

proc = subprocess.Popen(
    ["node", "D:/stellarismcp/stellaris_mcp_server/dist/index.js"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
    encoding="utf-8",
    bufsize=1
)

def send(obj):
    line = json.dumps(obj) + "\n"
    proc.stdin.write(line)
    proc.stdin.flush()
    resp = proc.stdout.readline()
    return json.loads(resp)

# 1. initialize
send({
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "test", "version": "1.0"}
    }
})

# 2. initialized notification
proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
proc.stdin.flush()

for key in ["PERDITION_BEAM_ION", "PLASMA_3", "DARK_MATTER_DEFLECTOR", "KINETIC_ARTILLERY_2"]:
    r = send({
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/call",
        "params": {
            "name": "stellaris_get_component_details",
            "arguments": {"key": key}
        }
    })
    txt = r["result"]["content"][0]["text"]
    parsed = json.loads(txt)
    print(f"\n=== KEY: {key} ===")
    print("Set Key:", parsed["set_key"])
    print("Localized Name:", parsed["localized_name"])
    print("Variants Count:", len(parsed["variants"]))
    for v in parsed["variants"]:
        print(f"  Variant: {v['component_key']} ({v['name']}) [size={v['size']}, power={v['power']}]")
        if "weapon_stats" in v:
            ws = v["weapon_stats"]
            print(f"    Damage: {ws['min_damage']} - {ws['max_damage']}, Range: {ws['range']}, CD: {ws['cooldown']}, Acc: {ws['accuracy']*100:.0f}%, Trk: {ws['tracking']*100:.0f}%")
            print(f"    Mult: Shield={ws['shield_mult']}x, Armor={ws['armor_mult']}x, Hull={ws['hull_mult']}x, Windup={ws['min_windup']}-{ws['max_windup']}")
        if "utility_stats" in v:
            us = v["utility_stats"]
            print(f"    Utility: {us}")

proc.terminate()
