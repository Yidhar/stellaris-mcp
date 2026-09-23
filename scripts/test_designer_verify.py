import json
import sys

def main():
    with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
        def call(method, params=None):
            req = {"jsonrpc": "2.0", "id": 1, "method": method, "params": params or {}}
            pipe.write((json.dumps(req) + "\n").encode("utf-8"))
            pipe.flush()
            line = pipe.readline().decode("utf-8")
            return json.loads(line)

        print("=== 1. Querying Designs ===")
        designs_resp = call("get_ship_designs")
        designs = designs_resp.get("result", {}).get("designs", [])
        print(f"Total designs: {len(designs)}")
        for d in designs:
            print(f"ID: {d.get('design_id')}, Name: {d.get('name')}, Size: {d.get('ship_size')}, Sections: {len(d.get('sections', []))}")
            for s in d.get('sections', []):
                print(f"   Section: {s.get('section_name')} ({s.get('section_key', '')}), Slots: {len(s.get('slots', []))}")
                for slot in s.get('slots', [])[:3]: # show first 3 slots
                    print(f"     Slot {slot.get('slot_index')} [{slot.get('slot_type')}]: {slot.get('component_key')} ({slot.get('component_name', '')})")

        print("\n=== 2. Querying Catalog (unlocked_only=True) ===")
        catalog_resp = call("get_ship_design_catalog", {"unlocked_only": True})
        comps = catalog_resp.get("result", {}).get("components", [])
        print(f"Total components in unlocked catalog: {len(comps)}")
        
        found_dragon = False
        kinetic_variants = []
        for c in comps:
            cat_name = c.get("category", "")
            for v in c.get("variants", []):
                k = v.get("key", "")
                n = v.get("name", "")
                if "DRAGON" in k or "龙鳞" in n:
                    found_dragon = True
                    print(f"WARNING: Dragon armor found: {k} ({n})")
                if "KINETIC" in k or "先进动能" in n or "动能火炮" in n:
                    kinetic_variants.append(f"{k} -> '{n}'")

        print(f"Dragon armor present in unlocked: {found_dragon}")
        print(f"Kinetic Artillery variants found ({len(kinetic_variants)}):")
        for kv in kinetic_variants:
            print(f"  {kv}")

        print("\n=== 3. Querying Catalog (unlocked_only=False) ===")
        all_catalog = call("get_ship_design_catalog", {"unlocked_only": False})
        all_comps = all_catalog.get("result", {}).get("components", [])
        all_dragon = False
        for c in all_comps:
            for v in c.get("variants", []):
                k = v.get("key", "")
                n = v.get("name", "")
                if "DRAGON" in k or "龙鳞" in n:
                    all_dragon = True
                    print(f"Found in all components: {k} ({n})")
        print(f"Dragon armor present in full catalog: {all_dragon}")

if __name__ == "__main__":
    main()
