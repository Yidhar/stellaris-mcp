import json

with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
    def call(m, p={}):
        pipe.write((json.dumps({"jsonrpc": "2.0", "id": 1, "method": m, "params": p}) + "\n").encode("utf-8"))
        pipe.flush()
        return json.loads(pipe.readline())

    print("=== Customizing Titan Design 1606 ===")
    slots_payload = [
        # Mid section: equip Advanced Kinetic Artillery in slot 0 & 1, Plasma in 2 & 3
        {"section_name": "mid", "slot_index": 0, "component_key": "KINETIC_ARTILLERY_2"},
        {"section_name": "mid", "slot_index": 1, "component_key": "KINETIC_ARTILLERY_2"},
        {"section_name": "mid", "slot_index": 2, "component_key": "LARGE_PLASMA_3"},
        {"section_name": "mid", "slot_index": 3, "component_key": "LARGE_PLASMA_3"},
        # Stern section: equip Advanced Kinetic Artillery in slot 0 & 1
        {"section_name": "stern", "slot_index": 0, "component_key": "KINETIC_ARTILLERY_2"},
        {"section_name": "stern", "slot_index": 1, "component_key": "KINETIC_ARTILLERY_2"},
        # Bow section: equip 3 Dark Matter Deflectors + 3 Neutron Armor
        {"section_name": "bow", "slot_index": 0, "component_key": "LARGE_DARK_MATTER_DEFLECTOR"},
        {"section_name": "bow", "slot_index": 1, "component_key": "LARGE_DARK_MATTER_DEFLECTOR"},
        {"section_name": "bow", "slot_index": 2, "component_key": "LARGE_DARK_MATTER_DEFLECTOR"},
        {"section_name": "bow", "slot_index": 3, "component_key": "LARGE_ARMOR_5"},
        {"section_name": "bow", "slot_index": 4, "component_key": "LARGE_ARMOR_5"},
        {"section_name": "bow", "slot_index": 5, "component_key": "LARGE_ARMOR_5"},
    ]

    cores_payload = {
        "reactor": "TITAN_DARK_MATTER_REACTOR",
        "ftl": "JUMP_DRIVE_1",
        "thruster": "TITAN_SHIP_THRUSTER_5",
        "sensor": "SENSOR_4",
        "combat_computer": "COMBAT_COMPUTER_ARTILLERY_SAPIENT",
        "aura": "SHIP_AURA_QUANTUM_DESTABILIZER"
    }

    upd_res = call("update_ship_design", {
        "design_id": 1606,
        "name": "天罚",
        "slots": slots_payload,
        "core_components": cores_payload
    })
    print("Update Result:", json.dumps(upd_res, ensure_ascii=False, indent=2))

    print("\n=== Reading back Titan 1606 design ===")
    d_res = call("get_ship_designs", {"design_id": 1606})
    titan = d_res.get("result", {}).get("designs", [])[0]
    print(f"ID: {titan['design_id']}, Name: {titan['name']}, Size: {titan['ship_size']}")
    print("Core Components:", json.dumps(titan.get("core_components"), ensure_ascii=False, indent=2))
    for s_idx, sec in enumerate(titan.get("sections", [])):
        print(f"\nSection '{sec.get('name')}':")
        for sl in sec.get("slots", []):
            print(f"  Slot {sl['slot_index']} [{sl.get('slot_name')}]: {sl.get('component_key')} ({sl.get('component_name')})")
