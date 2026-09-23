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

# 1. Initialize MCP
init_res = send({
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "test", "version": "1.0"}
    }
})

proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
proc.stdin.flush()

test_cases = [
    ("PERDITION_BEAM_ION", "Ion Cannon / Titan Main Guns"),
    ("PLASMA_3", "Plasma Accelerators (S / M / L)"),
    ("PLASMA", "All Plasma weapon sets"),
    ("DARK_MATTER_DEFLECTOR", "Dark Matter Deflector Shields (S / M / L)"),
    ("ARMOR_5", "Neutron Armor (S / M / L)"),
    ("SENSOR_4", "Tachyon Sensors & Detection"),
    ("STRIKE_CRAFT_HANGAR_3", "Advanced Strike Craft Hangar")
]

print("=================================================================")
print("  STELLARIS MCP NATIVE LIVE COMPONENT DETAILS FULL VERIFICATION  ")
print("=================================================================")

req_id = 10
for key, desc in test_cases:
    req_id += 1
    r = send({
        "jsonrpc": "2.0",
        "id": req_id,
        "method": "tools/call",
        "params": {
            "name": "stellaris_get_component_details",
            "arguments": {"key": key}
        }
    })
    txt = r["result"]["content"][0]["text"]
    data = json.loads(txt)

    print(f"\n[TEST CASE] Query: '{key}' ({desc})")
    if "matching_sets" in data:
        print(f"  -> Matched {data['matched_count']} sets:")
        for s in data["matching_sets"]:
            print(f"     * Set: {s['set_key']} ({s['localized_name']}) - {len(s['variants'])} variants")
    else:
        print(f"  -> Set: {data['set_key']} ({data['localized_name']}) - Total Variants: {data['total_variants']}")
        for v in data["variants"]:
            print(f"     * Variant: {v['component_key']} | Size: {v['size']:8} | Power: {v['power']:6} | Unlocked: {v['is_unlocked']}")
            if "weapon_stats" in v:
                ws = v["weapon_stats"]
                print(f"       Damage: {ws['min_damage']}-{ws['max_damage']} | Range: {ws['range']} | CD: {ws['cooldown']} | Acc: {ws['accuracy']*100:.0f}% | Trk: {ws['tracking']*100:.0f}%")
                print(f"       Mult: Hull {ws['hull_mult']}x | Armor {ws['armor_mult']}x | Shield {ws['shield_mult']}x | Windup: {ws['min_windup']}-{ws['max_windup']} days")
            if "utility_stats" in v:
                us = v["utility_stats"]
                print(f"       Utility Stats: {us}")
            if "strike_craft_stats" in v:
                sc = v["strike_craft_stats"]
                print(f"       Crafts: {sc['craft_count']} | Range: {sc['engagement_range']} | Dmg: {sc['min_damage']}-{sc['max_damage']} | CD: {sc['cooldown']} | Acc: {sc['accuracy']*100:.0f}%")

proc.terminate()
print("\n[+] FULL VERIFICATION COMPLETED SUCCESSFULLY.")
