"""Ship designer against the live game (save loaded, bridge injected).

Reads: the player's designable designs (sections, slots, required components) and the catalog.
Create / update go through CCreateOrUpdateShipDesignCommand, as the designer's save: a valid
create must show up with its edits; identical copies, taken names, components that do not fit a
slot or are not allowed on the hull must be refused; an update replaces the design (new id, same
name). Every design this test creates is deleted again.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_pipe  # noqa: E402

RESULTS = []


def record(name, status, detail=""):
    RESULTS.append((name, status))
    print(f"[{status}] {name}" + (f"  -- {detail}" if detail else ""))


class Pipe:
    def call(self, method, params=None):
        for _ in range(50):
            try:
                f = open(test_pipe.PIPE_PATH, "r+b", buffering=0)
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise RuntimeError("bridge pipe not available")
        f.write((json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params or {}}) + "\n").encode())
        buf = b""
        while not buf.endswith(b"\n"):
            buf += f.read(65536)
        f.close()
        r = json.loads(buf)
        return r.get("result", r)


def main():
    p = Pipe()

    def designs():
        return {d["design_id"]: d for d in p.call("get_ship_designs").get("designs", [])}

    def settle():
        # posted commands run on the next tick
        p.call("set_speed", {"speed": 1})
        p.call("set_paused", {"paused": False})
        time.sleep(2)
        p.call("set_paused", {"paused": True})

    p.call("set_paused", {"paused": True})
    before = designs()
    ok = bool(before) and all(d["sections"] and d["core_components"] for d in before.values())
    record("designable designs with sections and required components", "PASS" if ok else "FAIL", f"{len(before)} designs")
    cat = p.call("get_ship_design_catalog")
    record("component catalog", "PASS" if cat.get("components") else "FAIL", f"{cat.get('total_component_sets')} sets")

    proto = next((d for d in before.values() if d["sections"][0]["slots"]), None)
    if not proto:
        record("a design to start from", "SKIP")
        return 0
    size = proto["ship_size"]
    slot = next(s for s in proto["sections"][0]["slots"] if s["component_key"])
    # another component of the same family that the empire can use in that slot
    family = slot["component_key"].rsplit("_", 1)[0]
    variants = [v["component_key"] for st in cat.get("components", []) for v in st["variants"]]
    alt = next((v for v in variants if v != slot["component_key"] and v.rsplit("_", 1)[0] == family), None)

    name = f"MCP Test {int(time.time()) % 100000}"
    created = []
    if alt:
        r = p.call("create_ship_design", {"ship_size": size, "name": name,
                                          "slots": [{"section_index": 0, "slot_index": slot["slot_index"], "component_key": alt}]})
        record("create a design with a slot change", "PASS" if r.get("success") else "FAIL", r.get("error", r.get("message", "")))
        settle()
        new = [d for i, d in designs().items() if i not in before and d["name"] == name]
        got = new and new[0]["sections"][0]["slots"][slot["slot_index"]]["component_key"]
        record("the new design reads back with the change", "PASS" if got == alt else "FAIL", f"{got} (wanted {alt})")
        record("the prototype is untouched", "PASS" if proto["design_id"] in designs() else "FAIL")
        created += [d["design_id"] for d in new]
    else:
        record("create a design with a slot change", "SKIP", f"no alternative to {slot['component_key']}")

    r = p.call("create_ship_design", {"ship_size": size, "name": name + " copy"})
    record("an identical copy is refused", "PASS" if r.get("success") is False and "Identical" in r.get("error", "") else "FAIL",
           r.get("error", ""))
    r = p.call("create_ship_design", {"ship_size": size, "name": proto["name"],
                                      "slots": [{"section_index": 0, "slot_index": slot["slot_index"], "component_key": alt or slot["component_key"]}]})
    record("a taken name is refused", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))
    r = p.call("create_ship_design", {"ship_size": size, "name": name + " bad",
                                      "slots": [{"section_index": 0, "slot_index": slot["slot_index"], "component_key": "NO_SUCH_COMPONENT"}]})
    record("an unknown component is refused", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))
    wrong = next((v for v in variants if v.startswith(("LARGE_", "TITAN_", "EXTRA_LARGE_")) and v.rsplit("_", 1)[0] != family), None)
    if wrong and slot["slot_name"].startswith("SMALL_"):
        r = p.call("create_ship_design", {"ship_size": size, "name": name + " fit",
                                          "slots": [{"section_index": 0, "slot_index": slot["slot_index"], "component_key": wrong}]})
        record("a component that does not fit the slot is refused", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))
    other_hull = next((c["component_key"] for d in before.values() if d["ship_size"] != size
                       for c in d["core_components"] if "REACTOR" in c["component_key"]), None)
    if other_hull:
        r = p.call("create_ship_design", {"ship_size": size, "name": name + " core", "core_components": [other_hull]})
        record("another hull's reactor is refused", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))

    # update: put the original component back
    if created:
        did = created[0]
        r = p.call("update_ship_design", {"design_id": did,
                                          "slots": [{"section_index": 0, "slot_index": slot["slot_index"], "component_key": slot["component_key"]}]})
        record("update a design", "PASS" if r.get("success") else "FAIL", r.get("error", r.get("message", "")))
        settle()
        after = designs()
        replaced = [d for d in after.values() if d["name"] == name]
        record("the update replaced it under the same name with a new id",
               "PASS" if did not in after and len(replaced) == 1 else "FAIL", f"{[d['design_id'] for d in replaced]}")
        created = [d["design_id"] for d in replaced]
        r = p.call("update_ship_design", {"design_id": created[0] if created else did, "name": "Other"})
        record("renaming is refused", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))

    for did in created:
        p.call("delete_ship_design", {"design_id": did})
    settle()
    record("test designs deleted", "PASS" if not any(i in designs() for i in created) else "FAIL")
    record("bridge alive", "PASS" if p.call("ping").get("status") == "pong" else "FAIL")

    fails = [x for x in RESULTS if x[1] == "FAIL"]
    print(f"\n{sum(x[1] == 'PASS' for x in RESULTS)} passed, {len(fails)} failed, {sum(x[1] == 'SKIP' for x in RESULTS)} skipped")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
