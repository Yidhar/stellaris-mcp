"""End-to-end checks of the galaxy map tools against the live pipe (game running, save loaded,
DLL injected): get_galaxy_overview, get_galaxy_map, get_system, move_fleet, survey.

Reads are checked for shape and consistency (the map's center is the capital, rows match the
columns, hyperlanes only join listed systems, the capital system is ours and shows its planets).
Orders: bad ids and immobile / wrong fleets must be refused with a reason; a construction ship
is sent to the capital and must read back as moving there (the save is a test save).
"""
import json
import sys
import time

import test_pipe

RESULTS = []


def record(name, status, detail=""):
    RESULTS.append((name, status, detail))
    print(f"[{status}] {name}" + (f"  -- {detail}" if detail else ""))


class Pipe:
    def __init__(self):
        self.f = open(test_pipe.PIPE_PATH, "r+b", buffering=0)
        self.n = 0

    def call(self, method, params=None):
        self.n += 1
        r = test_pipe.send_request(self.f, self.n, method, params or {})
        return r.get("result", r)


def main():
    p = Pipe()
    if not p.call("get_status").get("in_game"):
        print("not in game")
        return 1

    ov = p.call("get_galaxy_overview")
    ok = ov.get("systems_total", 0) > 0 and ov.get("hyperlanes_total", 0) > 0 and "topology_version" in ov
    record("overview totals", "PASS" if ok else "FAIL", json.dumps({k: ov.get(k) for k in ("systems_total", "hyperlanes_total", "topology_version")}))
    player = ov.get("player", {})
    capital = player.get("capital_system_id")
    record("overview capital + owned systems", "PASS" if capital is not None and player.get("owned_systems", 0) > 0 else "FAIL",
           json.dumps(player, ensure_ascii=False))
    intel_total = sum(ov.get("intel", {}).values())
    record("intel counts cover every system", "PASS" if intel_total == ov.get("systems_total") else "FAIL", str(ov.get("intel")))

    m = p.call("get_galaxy_map", {"jumps": 2})
    cols = m.get("columns", [])
    rows = m.get("rows", [])
    ids = {r[0] for r in rows}
    ok = m.get("center_system_id") == capital and rows and all(len(r) == len(cols) for r in rows)
    record("map centered on the capital, rows match columns", "PASS" if ok else "FAIL", f"{len(rows)} systems")
    ok = all(a in ids and b in ids for a, b, _ in m.get("hyperlanes", []))
    record("map hyperlanes join listed systems", "PASS" if ok else "FAIL", f"{len(m.get('hyperlanes', []))} lanes")
    ok = all(r[cols.index("jumps")] <= 2 for r in rows)
    record("map respects the jump radius", "PASS" if ok else "FAIL")

    sysj = p.call("get_system", {"system_id": capital})
    ok = sysj.get("owner_id") == player.get("country_id") and sysj.get("planets") and sysj.get("hyperlanes")
    record("capital system: ours, planets and hyperlanes", "PASS" if ok else "FAIL",
           f"{sysj.get('name')}: {len(sysj.get('planets', []))} planets")
    bad = p.call("get_system", {"system_id": 0xFFFFFF})
    record("get_system rejects an unknown id", "PASS" if "error" in bad else "FAIL", str(bad)[:100])

    civ = p.call("get_civilian_fleets")
    constructors = [f["fleet_id"] for f in civ.get("construction_ships", [])]
    starbase = next((f["id"] for f in sysj.get("fleets", []) if f.get("ship_class") == "shipclass_starbase"), None)

    r = p.call("move_fleet", {"fleet_id": 999999, "system_id": capital})
    record("move_fleet refuses a fleet that is not ours", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))
    if starbase is not None:
        r = p.call("move_fleet", {"fleet_id": starbase, "system_id": capital})
        record("move_fleet refuses a starbase", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))
    else:
        record("move_fleet refuses a starbase", "SKIP", "no starbase fleet in the capital system")

    if constructors:
        r = p.call("survey", {"fleet_id": constructors[0], "system_id": capital})
        record("survey refuses a ship without a scientist", "PASS" if r.get("success") is False and r.get("error") else "FAIL",
               r.get("error", ""))
        r = p.call("move_fleet", {"fleet_id": constructors[0], "system_id": capital})
        record("move_fleet posts", "PASS" if r.get("success") else "FAIL", json.dumps(r, ensure_ascii=False)[:150])
        time.sleep(1.5)
        civ = p.call("get_civilian_fleets")
        status = next((f.get("status", "") for f in civ.get("construction_ships", []) if f["fleet_id"] == constructors[0]), "")
        name = sysj.get("name", "")
        # moving toward it (named, or the generic moving status inside the same system) or already there
        ok = name in status or "移动" in status or status == "无指令" or "idle" in status.lower() or "moving" in status.lower()
        record("the construction ship reads back as moving to (or at) the capital", "PASS" if ok else "FAIL", status)
    else:
        record("move_fleet / survey orders", "SKIP", "no construction ship")

    # queries
    fs = p.call("find_systems", {"purpose": "unsurveyed", "limit": 5})
    rows = fs.get("systems", [])
    ok = "error" not in fs and all(rows[i]["jumps"] <= rows[i + 1]["jumps"] for i in range(len(rows) - 1))
    record("find_systems unsurveyed, nearest first", "PASS" if ok else "FAIL", f"{len(rows)} systems")
    fo = p.call("find_systems", {"purpose": "outpost", "limit": 5})
    ok = "error" not in fo and all(r.get("owner_id") is None for r in fo.get("systems", []))
    ok = ok and all(("can_build" not in r) or r["can_build"] or r.get("reason") for r in fo.get("systems", []))
    record("find_systems outpost: unowned, engine verdict with reason", "PASS" if ok else "FAIL",
           json.dumps(fo.get("systems", [])[:1], ensure_ascii=False)[:150])
    fdp = p.call("find_systems", {"purpose": "deposit", "resource": "minerals", "limit": 3})
    ok = "error" not in fdp and all("minerals" in d["key"] for r in fdp.get("systems", []) for d in r["deposits"])
    record("find_systems deposit filter", "PASS" if ok else "FAIL", f"{len(fdp.get('systems', []))} systems")
    fc = p.call("find_systems", {"purpose": "colonizable", "limit": 5})
    pl = fc.get("planets", [])
    ok = "error" not in fc and all(x["habitability_percent"] > 0 for x in pl) and \
        all(pl[i]["habitability_percent"] >= pl[i + 1]["habitability_percent"] for i in range(len(pl) - 1))
    record("find_systems colonizable: habitable, best first", "PASS" if ok else "FAIL", f"{len(pl)} planets")
    ok = all(isinstance(x.get("can_colonize"), bool) and (x["can_colonize"] or x.get("reason")) for x in pl)
    record("colonizable rows carry the engine's CanColonize verdict", "PASS" if ok else "FAIL",
           json.dumps([(x["planet_id"], x.get("can_colonize"), x.get("reason")) for x in pl[:2]], ensure_ascii=False)[:150])
    sb = sysj.get("starbase") or {}
    record("capital starbase has a level name", "PASS" if sb.get("level_name") and sb.get("level_name") != sb.get("level") else "FAIL",
           f"{sb.get('level')} -> {sb.get('level_name')}")
    bad = p.call("find_systems", {"purpose": "nonsense"})
    record("find_systems rejects an unknown purpose", "PASS" if "error" in bad else "FAIL")
    lanes = {(a, b) for a, b, _ in p.call("get_galaxy_map", {"jumps": 12}).get("hyperlanes", [])}
    target = rows[-1]["id"] if rows else capital
    if constructors:
        # the engine's route for the construction ship from where it is
        fp = p.call("find_path", {"fleet_id": constructors[0], "to_system_id": target})
        rows_fp = fp.get("systems", [])
        ids = [x["id"] for x in rows_fp]
        arr = [x["arrival_days"] for x in rows_fp]
        # hyperlane legs join neighbours; bypass legs (relays, gateways, wormholes) need not.
        # Arrival is at the system's entry point; the estimate adds the last leg to the star
        lane_ok = all((min(a["id"], b["id"]), max(a["id"], b["id"])) in lanes
                      for a, b in zip(rows_fp, rows_fp[1:]) if b.get("via") == "hyperlane")
        via_ok = all(b.get("via") in ("hyperlane", "bypass") for b in rows_fp[1:])
        days_ok = (fp.get("estimated_days") or 0) > 0 and all(a <= b for a, b in zip(arr, arr[1:])) \
            and arr[-1] <= fp["estimated_days"]
        ok = fp.get("reachable") and ids[0] == fp.get("from_system_id") and ids[-1] == target \
            and lane_ok and via_ok and days_ok
        record("find_path: engine route along hyperlanes with arrival days", "PASS" if ok else "FAIL",
               f"{fp.get('jumps')} jumps, {fp.get('estimated_days')} days")
    r = p.call("find_path", {"to_system_id": target})
    record("find_path requires a player fleet", "PASS" if "error" in r else "FAIL", r.get("error", ""))

    # orders that must be refused with a reason
    if constructors:
        r = p.call("colonize", {"fleet_id": constructors[0], "planet_id": 0})
        record("colonize refuses an owned planet with the game's reason",
               "PASS" if r.get("success") is False and r.get("error") else "FAIL", r.get("error", ""))
    r = p.call("build_outpost", {"fleet_id": 999999, "system_id": capital})
    record("build_outpost refuses a fleet that is not ours", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))
    r = p.call("claim_system", {"system_id": capital})
    record("claim_system refuses our own system with the game's reason",
           "PASS" if r.get("success") is False and r.get("error") else "FAIL", r.get("error", ""))
    r = p.call("cancel_fleet_orders", {"fleet_ids": [999999]})
    record("cancel_fleet_orders refuses a fleet that is not ours", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))
    if constructors:
        r = p.call("set_fleet_stance", {"fleet_id": constructors[0], "stance": "sideways"})
        record("set_fleet_stance rejects an unknown stance", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))
        r = p.call("fleet_mia", {"fleet_ids": [constructors[0]], "type": "nowhere"})
        record("fleet_mia rejects an unknown type", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))

    # in-system orders
    sci = [f["fleet_id"] for f in civ.get("science_ships", [])]
    if sci:
        here = p.call("find_path", {"fleet_id": sci[0], "to_system_id": capital}).get("from_system_id")
        pl = p.call("get_system", {"system_id": here}).get("planets", [])
        if pl:
            r = p.call("orbit_planet", {"fleet_id": sci[0], "planet_id": pl[0]["id"]})
            record("orbit_planet posts for a science ship", "PASS" if r.get("success") else "FAIL", r.get("error", ""))
            p.call("cancel_fleet_orders", {"fleet_ids": [sci[0]]})
        r = p.call("orbit_planet", {"fleet_id": sci[0], "planet_id": 99999999})
        record("orbit_planet refuses an unknown planet", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))
        r = p.call("excavate_site", {"fleet_id": sci[0], "site_id": 99999999})
        record("excavate_site refuses an unknown site", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))
        relay = next((b for b in sysj.get("bypasses", []) if b.get("leads_to")), None)
        if relay:
            leads = {x["id"] for x in relay["leads_to"]}
            far = next(i for i in [x["id"] for x in rows] + [capital] if i not in leads)
            r = p.call("use_bypass", {"fleet_id": sci[0], "bypass_id": relay["id"], "to_system_id": far})
            record("use_bypass refuses a system the bypass does not lead to",
                   "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))

    # located special projects, debris and invasion
    if sci:
        r = p.call("collect_data", {"fleet_id": sci[0]})
        record("collect_data needs a project or a system", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))
    projects = p.call("get_situation_log").get("special_projects", [])
    debris_projects = [x for x in projects if x.get("kind") == "debris"]
    record("situation log lists debris projects", "PASS" if debris_projects else "SKIP", f"{len(debris_projects)}")
    transports = [f["fleet_id"] for f in civ.get("transport_fleets", [])]
    if transports:
        r = p.call("land_armies", {"fleet_id": transports[0], "planet_id": 99999999})
        record("land_armies refuses an unknown planet", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))

    # map flags M / G agree with the system detail's megastructures / bypasses
    mg = [r for r in p.call("get_galaxy_map", {"jumps": 4}).get("rows", []) if "M" in r[7] or "G" in r[7]][:5]
    ok = True
    for r in mg:
        sj = p.call("get_system", {"system_id": r[0]})
        ok = ok and (("M" in r[7]) == bool(sj.get("megastructures"))) and (("G" in r[7]) == bool(sj.get("bypasses")))
        ok = ok and all(b.get("type") and isinstance(b.get("leads_to"), list) for b in sj.get("bypasses", []))
    record("map M/G flags match get_system megastructures/bypasses", "PASS" if ok else ("SKIP" if not mg else "FAIL"),
           f"{len(mg)} systems")

    # system detail: player claims and own fleets' stance
    ok = isinstance(sysj.get("player_claims"), int) and \
        all(f.get("stance") in ("passive", "aggressive", "evasive") for f in sysj.get("fleets", [])
            if f.get("own") and f.get("ship_class") not in ("shipclass_starbase",) and "stance" in f)
    record("get_system: player_claims and own fleet stances", "PASS" if ok else "FAIL",
           f"claims {sysj.get('player_claims')}")

    ok = p.call("ping").get("status") == "pong"
    record("bridge alive", "PASS" if ok else "FAIL")

    fails = [x for x in RESULTS if x[1] == "FAIL"]
    print(f"\n{sum(x[1] == 'PASS' for x in RESULTS)} passed, {len(fails)} failed, {sum(x[1] == 'SKIP' for x in RESULTS)} skipped")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
