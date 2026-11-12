"""End-to-end check of construction ship stations through the built MCP server (stdio): a station
the game accepts is ordered (the ship's orders show it), refusals carry the game's reasons.

Needs the game running with the bridge injected. Loads the test campaign's manual save
(TEST_CAMPAIGN / 2225.01.01.sav): PLANET is a deposit planet without a station there, EARTH an
inhabited one. The order is cancelled at the end.

    python scripts/test_mcp_station_end_to_end.py
"""
import sys
import time

from test_mcp_session_end_to_end import Mcp, RESULTS, TEST_CAMPAIGN, record

PLANET = 1244  # 尼万-A, energy deposit in a system of the player's
EARTH = 3
MOON = 4       # minerals deposit with a mining station already


def run(m):
    ok = False
    for _ in range(30):
        ok, r = m.tool("stellaris_load_game", {"campaign": TEST_CAMPAIGN, "file": "2225.01.01.sav"})
        if ok:
            break
        time.sleep(10)
    record("test save loaded", ok, str(r)[:100])
    if not ok:
        return 1
    ok, civ = m.tool("stellaris_get_civilian_fleets")
    fid = civ["construction_ships"][0]["fleet_id"]

    ok, r = m.tool("stellaris_build_station", {"fleet_id": fid, "planet_id": MOON})
    record("a planet with a station already: the game's reason", ok and not r.get("success") and r.get("reasons"), str(r.get("reasons"))[:140])
    ok, r = m.tool("stellaris_build_station", {"fleet_id": fid, "planet_id": EARTH, "station": "research"})
    record("an inhabited planet: refused with the game's reason", ok and not r.get("success") and r.get("reasons", {}).get("research"),
           str(r.get("reasons"))[:140])
    ok, r = m.tool("stellaris_build_station", {"fleet_id": fid, "planet_id": PLANET})
    record("a mining station ordered", ok and r.get("success") and r.get("station") == "mining", str(r)[:140])
    ok, civ = m.tool("stellaris_get_civilian_fleets")
    status = next(f.get("status", "") for f in civ["construction_ships"] if f["fleet_id"] == fid)
    record("the ship is on it", "%" in status or "建造" in status, status)
    m.tool("stellaris_cancel_fleet_orders", {"fleet_ids": [fid]})
    return 0


def main():
    m = Mcp()
    rc = 1
    try:
        rc = run(m)
    finally:
        m.close()
    fails = [x for x in RESULTS if x[1] == "FAIL"]
    print(f"\n{len(RESULTS) - len(fails)} passed, {len(fails)} failed")
    return 1 if fails or rc else 0


if __name__ == "__main__":
    sys.exit(main())
