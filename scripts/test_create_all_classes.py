import json

with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
    def call(m, p={}):
        pipe.write((json.dumps({"jsonrpc": "2.0", "id": 1, "method": m, "params": p}) + "\n").encode("utf-8"))
        pipe.flush()
        return json.loads(pipe.readline())

    designs_to_create = [
        {
            "ship_size": "corvette",
            "name": "暗夜突袭",
            "slots": [
                {"section_name": "mid", "slot_index": 0, "component_key": "SMALL_PLASMA_3"},
                {"section_name": "mid", "slot_index": 1, "component_key": "SMALL_PLASMA_3"},
                {"section_name": "mid", "slot_index": 2, "component_key": "SMALL_PLASMA_3"},
                {"section_name": "mid", "slot_index": 3, "component_key": "SMALL_DARK_MATTER_DEFLECTOR"},
                {"section_name": "mid", "slot_index": 4, "component_key": "SMALL_ARMOR_5"},
                {"section_name": "mid", "slot_index": 5, "component_key": "SMALL_DARK_MATTER_DEFLECTOR"},
                {"section_name": "mid", "slot_index": 6, "component_key": "AFTERBURNER_2"},
            ],
            "core_components": {
                "reactor": "CORVETTE_DARK_MATTER_REACTOR",
                "ftl": "JUMP_DRIVE_1",
                "thruster": "SHIP_THRUSTER_5",
                "sensor": "SENSOR_4",
                "combat_computer": "COMBAT_COMPUTER_SWARM_SAPIENT"
            }
        },
        {
            "ship_size": "destroyer",
            "name": "雷霆护卫",
            "slots": [
                {"section_name": "bow", "slot_name": "MEDIUM_GUN_01", "component_key": "MEDIUM_PLASMA_3"},
                {"section_name": "bow", "slot_name": "SMALL_GUN_01", "component_key": "SMALL_PLASMA_3"},
                {"section_name": "bow", "slot_name": "SMALL_GUN_02", "component_key": "SMALL_PLASMA_3"},
                {"section_name": "bow", "slot_name": "SMALL_UTILITY_1", "component_key": "SMALL_DARK_MATTER_DEFLECTOR"},
                {"section_name": "bow", "slot_name": "SMALL_UTILITY_2", "component_key": "SMALL_ARMOR_5"},
                {"section_name": "stern", "slot_name": "SMALL_GUN_01", "component_key": "SMALL_PLASMA_3"},
                {"section_name": "stern", "slot_name": "SMALL_GUN_02", "component_key": "SMALL_PLASMA_3"},
                {"section_name": "stern", "slot_name": "AUX_UTILITY_1", "component_key": "AFTERBURNER_2"},
                {"section_name": "stern", "slot_name": "AUX_UTILITY_2", "component_key": "AFTERBURNER_2"},
            ],
            "core_components": {
                "reactor": "DESTROYER_DARK_MATTER_REACTOR",
                "ftl": "JUMP_DRIVE_1",
                "thruster": "DESTROYER_SHIP_THRUSTER_5",
                "sensor": "SENSOR_4",
                "combat_computer": "COMBAT_COMPUTER_LINE_SAPIENT"
            }
        },
        {
            "ship_size": "cruiser",
            "name": "远征先锋",
            "slots": [
                {"section_name": "bow", "slot_name": "MEDIUM_GUN_01", "component_key": "MEDIUM_PLASMA_3"},
                {"section_name": "bow", "slot_name": "MEDIUM_GUN_02", "component_key": "MEDIUM_PLASMA_3"},
                {"section_name": "mid", "slot_name": "MEDIUM_GUN_01", "component_key": "MEDIUM_PLASMA_3"},
                {"section_name": "mid", "slot_name": "MEDIUM_GUN_02", "component_key": "MEDIUM_PLASMA_3"},
                {"section_name": "mid", "slot_name": "MEDIUM_GUN_03", "component_key": "MEDIUM_PLASMA_3"},
                {"section_name": "stern", "slot_name": "SMALL_GUN_01", "component_key": "SMALL_PLASMA_3"},
                {"section_name": "stern", "slot_name": "SMALL_GUN_02", "component_key": "SMALL_PLASMA_3"},
                {"section_name": "stern", "slot_name": "AUX_UTILITY_1", "component_key": "AFTERBURNER_2"},
            ],
            "core_components": {
                "reactor": "CRUISER_DARK_MATTER_REACTOR",
                "ftl": "JUMP_DRIVE_1",
                "thruster": "CRUISER_SHIP_THRUSTER_5",
                "sensor": "SENSOR_4",
                "combat_computer": "COMBAT_COMPUTER_LINE_SAPIENT"
            }
        },
        {
            "ship_size": "battleship",
            "name": "灭绝之星",
            "slots": [
                {"section_name": "bow", "slot_name": "LARGE_GUN_01", "component_key": "LARGE_PLASMA_3"},
                {"section_name": "mid", "slot_name": "LARGE_GUN_01", "component_key": "KINETIC_ARTILLERY_2"},
                {"section_name": "mid", "slot_name": "LARGE_GUN_02", "component_key": "KINETIC_ARTILLERY_2"},
                {"section_name": "mid", "slot_name": "MEDIUM_GUN_01", "component_key": "MEDIUM_PLASMA_3"},
                {"section_name": "mid", "slot_name": "MEDIUM_GUN_02", "component_key": "MEDIUM_PLASMA_3"},
                {"section_name": "stern", "slot_name": "MEDIUM_GUN_01", "component_key": "MEDIUM_PLASMA_3"},
                {"section_name": "stern", "slot_name": "MEDIUM_GUN_02", "component_key": "MEDIUM_PLASMA_3"},
                {"section_name": "stern", "slot_name": "AUX_UTILITY_1", "component_key": "AFTERBURNER_2"},
            ],
            "core_components": {
                "reactor": "BATTLESHIP_DARK_MATTER_REACTOR",
                "ftl": "JUMP_DRIVE_1",
                "thruster": "BATTLESHIP_SHIP_THRUSTER_5",
                "sensor": "SENSOR_4",
                "combat_computer": "COMBAT_COMPUTER_ARTILLERY_SAPIENT"
            }
        }
    ]

    created_ids = []
    for cfg in designs_to_create:
        print(f"\n[*] Creating design '{cfg['name']}' ({cfg['ship_size']})...")
        c_res = call("create_ship_design", cfg)
        print("Create response:", json.dumps(c_res, ensure_ascii=False))
        if c_res.get("result", {}).get("success"):
            did = c_res["result"]["design_id"]
            created_ids.append((did, cfg["name"], cfg["ship_size"]))

    print("\n==================================================")
    print("VERIFICATION OF ALL NEW DESIGNS:")
    for did, name, size in created_ids:
        v_res = call("get_ship_designs", {"design_id": did})
        d = v_res.get("result", {}).get("designs", [])[0]
        print(f"\n[+] Verified ID {did}: '{name}' ({size})")
        print(f"    Cores: {d['core_components']}")
        for sec in d["sections"]:
            equipped_slots = [f"{s['slot_name']}={s['component_key']}" for s in sec["slots"] if s["component_key"]]
            print(f"    Sec '{sec['name']}': {', '.join(equipped_slots)}")
