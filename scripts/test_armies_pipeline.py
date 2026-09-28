import json
import sys
sys.stdout.reconfigure(encoding='utf-8')
import test_pipe
import time

def test():
    with open(test_pipe.PIPE_PATH, "r+b", buffering=0) as pipe:
        print("=================================================================")
        print("    Stellaris MCP Bridge - 行星陆军体系与状态控制全链路测试")
        print("=================================================================")

        # 1. 验证 get_planet_details 中的 armies_summary (Layer 1)
        print("\n[Step 1] 验证 get_planet_details 中的 armies_summary (Layer 1)...")
        resp1 = test_pipe.send_request(pipe, 1, "get_planet_details", {"planet_id": 3})
        print("DEBUG resp1:", resp1)
        res1 = resp1.get("result", {})
        armies_sum = res1.get("armies_summary", {})
        print("Armies Summary in get_planet_details:")
        print(json.dumps(armies_sum, indent=2, ensure_ascii=False))
        assert "total_stationed_armies" in armies_sum, "Missing total_stationed_armies in summary"
        assert armies_sum["total_stationed_armies"] == 7, f"Expected 7 armies, got {armies_sum['total_stationed_armies']}"
        print(f"  [+] Earth stationed armies: {armies_sum['total_stationed_armies']} (Defense: {armies_sum['defense_armies_count']}, Assault: {armies_sum['assault_armies_count']})")
        print(f"  [+] Garrison power: {armies_sum['garrison_power']}, Assault power: {armies_sum['assault_power']}, Total: {armies_sum['total_power']}")
        print(f"  [+] deploy_in_orbit: {armies_sum['deploy_in_orbit']}, include_in_builder: {armies_sum['include_in_builder']}")

        # 2. 验证 get_planet_armies (Layer 2 详细视图)
        print("\n[Step 2] 验证 get_planet_armies (Layer 2 陆军详情视图)...")
        resp2 = test_pipe.send_request(pipe, 2, "get_planet_armies", {"planet_id": 3})
        res2 = resp2.get("result", {})
        print(f"Planet: {res2.get('planet_name')} (ID: {res2.get('planet_id')})")
        
        stationed = res2.get("stationed_armies", [])
        print(f"  [+] Stationed armies count: {len(stationed)}")
        for idx, a in enumerate(stationed):
            print(f"      Army {idx+1}: ID={a['army_id']} ({a['name']}), Key={a['type_key']}, IsDefense={a['is_defense']}, Power={a['power']}, HP={a['health']}/{a['max_health']} ({a['health_percent']}%), Species={a['species_name']}")

        recruitable = res2.get("recruitable_armies", [])
        print(f"\n  [+] Recruitable assault armies: {len(recruitable)}")
        for r in recruitable:
            print(f"      - {r['key']} ({r['name']}): build_time={r['build_time_days']}d, base_power={r['base_power']}")

        queue = res2.get("construction_queue", [])
        print(f"\n  [+] Army recruitment queue count: {len(queue)}")

        # 3. 验证两个状态控制: set_planet_army_settings
        print("\n[Step 3] 验证状态控制 1: deploy_in_orbit (部署到轨道上)...")
        # Earth is initially deploy_in_orbit = False. Let's toggle to True!
        toggle_on = test_pipe.send_request(pipe, 3, "set_planet_army_settings", {"planet_id": 3, "deploy_in_orbit": True})
        print("Toggle deploy_in_orbit -> True command dispatched:", json.dumps(toggle_on.get("result", {}), indent=2, ensure_ascii=False))
        time.sleep(0.1)
        check1 = test_pipe.send_request(pipe, 31, "get_planet_details", {"planet_id": 3}).get("result", {}).get("armies_summary", {})
        print("Authoritative deploy_in_orbit in engine memory:", check1.get("deploy_in_orbit"))
        assert check1.get("deploy_in_orbit") == True, "Failed to enable deploy_in_orbit in memory"

        # Toggle back to False
        toggle_off = test_pipe.send_request(pipe, 4, "set_planet_army_settings", {"planet_id": 3, "deploy_in_orbit": False})
        print("Toggle deploy_in_orbit -> False command dispatched:", json.dumps(toggle_off.get("result", {}), indent=2, ensure_ascii=False))
        time.sleep(0.1)
        check2 = test_pipe.send_request(pipe, 41, "get_planet_details", {"planet_id": 3}).get("result", {}).get("armies_summary", {})
        print("Authoritative deploy_in_orbit restored in memory:", check2.get("deploy_in_orbit"))
        assert check2.get("deploy_in_orbit") == False, "Failed to restore deploy_in_orbit in memory"

        print("\n[Step 4] 验证状态控制 2: include_in_builder (包含在陆军建造功能中)...")
        # Earth is initially include_in_builder = True. Let's toggle to False!
        builder_off = test_pipe.send_request(pipe, 5, "set_planet_army_settings", {"planet_id": 3, "include_in_builder": False})
        print("Toggle include_in_builder -> False command dispatched:", json.dumps(builder_off.get("result", {}), indent=2, ensure_ascii=False))
        time.sleep(0.1)
        check3 = test_pipe.send_request(pipe, 51, "get_planet_details", {"planet_id": 3}).get("result", {}).get("armies_summary", {})
        print("Authoritative include_in_builder in engine memory:", check3.get("include_in_builder"))
        assert check3.get("include_in_builder") == False, "Failed to disable include_in_builder in memory"

        # Toggle back to True
        builder_on = test_pipe.send_request(pipe, 6, "set_planet_army_settings", {"planet_id": 3, "include_in_builder": True})
        print("Toggle include_in_builder -> True command dispatched:", json.dumps(builder_on.get("result", {}), indent=2, ensure_ascii=False))
        time.sleep(0.1)
        check4 = test_pipe.send_request(pipe, 61, "get_planet_details", {"planet_id": 3}).get("result", {}).get("armies_summary", {})
        print("Authoritative include_in_builder restored in memory:", check4.get("include_in_builder"))
        assert check4.get("include_in_builder") == True, "Failed to restore include_in_builder in memory"

        print("\n=================================================================")
        print(" [SUCCESS] 行星陆军 Layer 1/2 数据直读与双状态控制已全部通过测试！")
        print("=================================================================")

if __name__ == "__main__":
    test()
