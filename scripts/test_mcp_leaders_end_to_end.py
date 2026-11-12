"""End-to-end check of the leader tools through the built MCP server (stdio): the leader list's
locations, hiring from the pool, where a leader can serve (the game's IsValid), assigning a scientist
to a ship and onto the council, and the refusals with a reason.

Needs the game running with the bridge injected. Loads the test campaign's manual save
(TEST_CAMPAIGN / 2225.01.01.sav) and changes leaders in that game (not saved).

    python scripts/test_mcp_leaders_end_to_end.py
"""
import sys
import time

from test_mcp_session_end_to_end import Mcp, RESULTS, TEST_CAMPAIGN, record


def leader(m, lid):
    ok, L = m.tool("stellaris_get_leaders")
    return next((x for x in L.get("hired_leaders", []) if x["id"] == lid), None) if ok else None


def settle(m):
    # the game runs posted commands as time passes
    m.tool("stellaris_run_until", {"days": 1, "max_seconds": 20})


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

    ok, L = m.tool("stellaris_get_leaders")
    hired = L.get("hired_leaders", []) if ok else []
    record("leaders carry their location and council seat", ok and hired and all("assignment_type" in x and "council_position_id" in x for x in hired),
           f"{len(hired)} leaders")
    record("the ruler and the councilors are read as such", any(x["assignment_type"] == "ruler" for x in hired)
           and sum(1 for x in hired if x["council_position_id"] != 4294967295) >= 2)
    record("leader names are plain", all("|||" not in x["name"] for x in hired + L.get("pool_candidates", [])))

    cand = next((c for c in L.get("pool_candidates", []) if c["class"] == "scientist"), None)
    if not cand:
        print("[SKIP] no scientist in the pool")
        return 0
    ok, r = m.tool("stellaris_hire_leader", {"candidate_id": cand["id"]})
    record("hire a scientist from the pool", ok and r.get("success"), str(r)[:120])
    settle(m)
    me = leader(m, cand["id"])
    record("the hired scientist is a leader, unassigned", me is not None and me["assignment_type"] == "unassigned", str(me and me["assignment_type"]))
    if not me:
        return 1

    ok, a = m.tool("stellaris_get_leader_assignments", {"leader_id": me["id"]})
    record("where the scientist can serve: slots and council seats", ok and isinstance(a.get("slots"), list) and a.get("council"),
           f"{len(a.get('slots', []))} slots, {len(a.get('council', []))} seats" if ok else str(a)[:120])
    record("refused council seats say why", ok and all(s.get("can_assign") or s.get("reason") for s in a.get("council", [])),
           str([s.get("reason") for s in a.get("council", []) if not s.get("can_assign")])[:160])

    ship = next((s for s in a.get("slots", []) if s["slot"] == "ship"), None)
    if ship:
        ok, r = m.tool("stellaris_assign_leader", {"leader_id": me["id"], "slot": "ship", "target_id": ship["target_id"]})
        record("assign the scientist to a ship", ok, str(r)[:120])
        settle(m)
        me = leader(m, me["id"])
        record("the scientist now leads that ship", me["assignment_type"] == "ship", f"{me['assignment_type']} {me['target_id']}")
    else:
        print("[SKIP] no ship without a leader takes this scientist")

    seat = next((s for s in a.get("council", []) if s.get("can_assign")), None)
    if seat:
        ok, r = m.tool("stellaris_assign_leader", {"leader_id": me["id"], "slot": "council", "target_id": seat["target_id"]})
        record("put the scientist onto the council", ok, str(r)[:120])
        settle(m)
        me = leader(m, me["id"])
        record("the scientist holds that seat", me["council_position_id"] == seat["target_id"] and me["is_councilor"], str(me["council_position_id"]))
        ok, r = m.tool("stellaris_assign_leader", {"leader_id": me["id"], "slot": "council", "target_id": seat["target_id"]})
        record("the same seat again is refused with a reason", not ok and "already" in str(r), str(r)[:120])
    else:
        print("[SKIP] no council seat takes this scientist")

    ok, r = m.tool("stellaris_assign_leader", {"leader_id": me["id"], "slot": "ship", "target_id": 4000000000})
    record("an unknown fleet is refused", not ok, str(r)[:100])
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
