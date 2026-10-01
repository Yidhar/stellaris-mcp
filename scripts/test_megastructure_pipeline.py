"""End-to-end checks of the megastructure tools against the live pipe (game running, save loaded,
DLL injected): get_megastructure, upgrade_megastructure, get_buildable_megastructures,
build_megastructure.

Reads only, plus orders the game must refuse: the player's megastructures (found through
get_galaxy_map / get_system) read back with their upgrades and costs, a construction ship's build
menu lists first stages with a placement and a cost, sites are planets or hyperlane neighbours
of the system asked about. Nothing is built or upgraded, so the save is left as it was.
"""
import sys

import test_pipe

RESULTS = []
PLACEMENTS = {"planet", "inside_gravity_well", "outside_gravity_well"}


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
    player = p.call("get_galaxy_overview").get("player", {})
    me, capital = player.get("country_id"), player.get("capital_system_id")
    record("overview names the player's country", "PASS" if me is not None else "FAIL", str(me))

    # the player's megastructures: systems flagged M that the player owns
    gm = p.call("get_galaxy_map", {"jumps": 12})
    cols = gm.get("columns", ["id", "name", "x", "y", "owner_id", "intel", "jumps", "flags"])
    rows = [dict(zip(cols, r)) for r in gm.get("rows", [])]
    mine = []
    for r in rows:
        if "M" in (r.get("flags") or "") and r.get("owner_id") == me:
            for m in p.call("get_system", {"system_id": r["id"]}).get("megastructures", []):
                if m.get("owner_id") == me:
                    mine.append(m["id"])
    if mine:
        ok = True
        for mid in mine[:8]:
            d = p.call("get_megastructure", {"megastructure_id": mid})
            ups = d.get("upgrades")
            ok &= d.get("id") == mid and d.get("owner_id") == me and bool(d.get("type")) and isinstance(ups, list)
            ok &= all(isinstance(u.get("possible"), bool) and isinstance(u.get("cost"), dict) and u.get("type") for u in ups)
            ok &= all(u.get("reason") for u in ups if not u["possible"])
        record("get_megastructure: own megastructures with upgrades, costs, reasons", "PASS" if ok else "FAIL",
               f"{len(mine)} found")
    else:
        record("get_megastructure: own megastructures with upgrades, costs, reasons", "SKIP", "none within 12 jumps")
    r = p.call("get_megastructure", {"megastructure_id": 0xFFFFFF})
    record("get_megastructure rejects an unknown id", "PASS" if "error" in r else "FAIL", r.get("error", ""))
    if mine:
        r = p.call("upgrade_megastructure", {"megastructure_id": mine[0], "type": "no_such_stage"})
        record("upgrade_megastructure refuses a stage it does not offer", "PASS" if r.get("success") is False else "FAIL",
               r.get("error", ""))

    civ = p.call("get_civilian_fleets", {})
    ships = [f["fleet_id"] for f in civ.get("construction_ships", [])]
    if not ships:
        record("construction ship menu", "SKIP", "no construction ship")
    else:
        menu = p.call("get_buildable_megastructures", {"fleet_id": ships[0]}).get("megastructures", [])
        ok = all(m.get("type") and m.get("placement") in PLACEMENTS and isinstance(m.get("cost"), dict) for m in menu)
        record("build menu: types with placement and cost", "PASS" if ok else "FAIL", f"{len(menu)} types")
        if capital is not None:
            sysj = p.call("get_system", {"system_id": capital})
            planets = {pl["id"] for pl in sysj.get("planets", [])}  # stars included
            lanes = {h["to"] for h in sysj.get("hyperlanes", [])}
            at = p.call("get_buildable_megastructures", {"fleet_id": ships[0], "system_id": capital}).get("megastructures", [])
            ok = True
            for m in at:
                for s in m.get("sites", []):
                    ok &= (s.get("planet_id") in planets) if m["placement"] == "planet" else (s.get("toward_system_id") in lanes)
                ok &= bool(m.get("sites")) or bool(m.get("reason"))
            record("sites are the system's planets / hyperlane neighbours, else a reason", "PASS" if ok else "FAIL",
                   f"{sum(len(m.get('sites', [])) for m in at)} sites")
            open_space = next((m["type"] for m in menu if m["placement"] != "planet"), None)
            if open_space:
                r = p.call("build_megastructure", {"fleet_id": ships[0], "type": open_space, "planet_id": 0})
                record("build_megastructure asks open-space types for a point", "PASS" if r.get("success") is False else "FAIL",
                       r.get("error", ""))
        r = p.call("build_megastructure", {"fleet_id": ships[0], "type": "no_such_type", "planet_id": 0})
        record("build_megastructure rejects an unknown type", "PASS" if r.get("success") is False else "FAIL", r.get("error", ""))

    ok = p.call("ping").get("status") == "pong"
    record("bridge alive", "PASS" if ok else "FAIL")
    fails = [x for x in RESULTS if x[1] == "FAIL"]
    print(f"\n{sum(x[1] == 'PASS' for x in RESULTS)} passed, {len(fails)} failed, {sum(x[1] == 'SKIP' for x in RESULTS)} skipped")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
