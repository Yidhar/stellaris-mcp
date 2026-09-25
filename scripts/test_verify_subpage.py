import json
import sys
sys.stdout.reconfigure(encoding='utf-8')
import test_pipe

def test():
    with open(test_pipe.PIPE_PATH, "r+b", buffering=0) as pipe:
        print("=== Test 1: get_planet_details ===")
        resp = test_pipe.send_request(pipe, 1, "get_planet_details", {"planet_id": 3})
        res = resp.get("result", {})
        
        print("\n--- Summary / Overview ---")
        print(f"Planet: {res.get('name')} (ID: {res.get('planet_id')})")
        print(f"Colony ID: {res.get('colony_id')}")
        print(f"Designation: {res.get('designation')}")

        print("\n--- Planetary Features Summary ---")
        features = res.get("planetary_features", {})
        print("Features summary:", json.dumps(features.get("summary"), indent=2, ensure_ascii=False))
        print(f"Total features list length: {len(features.get('features', []))}")
        for f in features.get("features", []):
            mods = [m.get("formatted") for m in f.get("modifiers", [])]
            swap = f.get("swap_type")
            swap_info = f" -> swap: {swap.get('name')} ({[m.get('formatted') for m in swap.get('modifiers', [])]})" if swap else ""
            print(f"  [{'Blocker' if f.get('is_blocker') else 'Natural'}] {f.get('name')} ({f.get('key')}): mods={mods}, clear_time={f.get('clear_time')}d, cost={f.get('clear_cost')}{swap_info}, status='{f.get('status_text')}'")

        print("\n--- Current Population Breakdown ---")
        pop_breakdown = res.get("population_breakdown", [])
        for p in pop_breakdown:
            print(f"  Species {p.get('species_name')} (ID {p.get('species_id')}): count={p.get('pop_count')} ({p.get('pop_count_display')}), share={p.get('share_percent')}%, net_change={p.get('net_change')}, portrait={p.get('portrait')}")

        print("\n--- Monthly Population Summary ---")
        m_summary = res.get("monthly_population_summary", {})
        print(f"  Net Change: {m_summary.get('net_change')}")
        print(f"  Growth: {m_summary.get('growth')}")
        print(f"  Migration: {m_summary.get('migration')}")
        print(f"  Assembly: {m_summary.get('assembly')}")
        print(f"  Categories: {json.dumps(m_summary.get('categories'), ensure_ascii=False)}")
        print(f"  Demographics: {json.dumps(m_summary.get('demographics'), ensure_ascii=False)}")

        print("\n--- Colony Ascension ---")
        asc = res.get("colony_ascension", {})
        print(f"  Tier: {asc.get('tier')} ({asc.get('tier_name')})")
        print(f"  Multiplier: {asc.get('designation_multiplier_percent')}%")
        print(f"  Can Ascend: {asc.get('can_ascend')}")
        print(f"  Desc: {asc.get('description')}")
        print(f"  Status: {asc.get('status_desc')}")

        print("\n=== Test 2: get_planetary_features ===")
        resp_feat = test_pipe.send_request(pipe, 2, "get_planetary_features", {"planet_id": 3})
        print("get_planetary_features result:", json.dumps(resp_feat.get("result", {}).get("summary"), indent=2, ensure_ascii=False))

        print("\n=== Test 3: ascend_colony ===")
        resp_asc = test_pipe.send_request(pipe, 3, "ascend_colony", {"planet_id": 3})
        print("ascend_colony result:", json.dumps(resp_asc, indent=2, ensure_ascii=False))

if __name__ == "__main__":
    test()
