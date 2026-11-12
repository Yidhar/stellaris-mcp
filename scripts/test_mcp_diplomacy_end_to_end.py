"""End-to-end check of proposing diplomacy through the built MCP server (stdio): the actions toward an
empire with the game's verdicts, closing and reopening borders (seen in the action list), a refusal
with the game's reason, and an envoy sent to improve relations.

Needs the game running with the bridge injected. Loads the test campaign's manual save
(TEST_CAMPAIGN / 2225.01.01.sav); EMPIRE is a regular empire known there. Changes that game (not saved).

    python scripts/test_mcp_diplomacy_end_to_end.py
"""
import sys
import time

from test_mcp_session_end_to_end import Mcp, RESULTS, TEST_CAMPAIGN, record

EMPIRE = 31  # 忒伊亚保护者联邦 in the test save


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

    def actions():
        ok, a = m.tool("stellaris_get_diplomatic_actions", {"country_id": EMPIRE})
        return {x["action"]: x for x in a.get("actions", [])} if ok else {}

    before = actions()
    record("actions toward the empire, with the game's verdict", len(before) >= 10 and any(x["can_propose"] for x in before.values()),
           f"{len(before)} actions")
    record("refused actions carry the game's reason", all(x["can_propose"] or x.get("reason") for x in before.values()))

    if before.get("action_close_borders", {}).get("can_propose"):
        ok, r = m.tool("stellaris_propose_diplomatic_action", {"country_id": EMPIRE, "action": "action_close_borders"})
        record("propose closing borders", ok and r.get("success"), str(r)[:120])
        m.tool("stellaris_run_until", {"days": 2, "max_seconds": 20})
        after = actions()
        record("borders closed: the game now offers to open them", "action_open_borders" in after and "action_close_borders" not in after)
        ok, r = m.tool("stellaris_propose_diplomatic_action", {"country_id": EMPIRE, "action": "action_open_borders"})
        m.tool("stellaris_run_until", {"days": 2, "max_seconds": 20})
        record("and reopened", "action_close_borders" in actions(), str(r)[:120])
    else:
        print("[SKIP] closing borders is not offered")

    refused = next((k for k, x in before.items() if not x["can_propose"]), None)
    ok, r = m.tool("stellaris_propose_diplomatic_action", {"country_id": EMPIRE, "action": refused})
    record("a refused action is not posted, with the game's reason", not r.get("success") and len(str(r.get("error", ""))) > 10, str(r)[:140])
    ok, r = m.tool("stellaris_propose_diplomatic_action", {"country_id": EMPIRE, "action": "no_such_action"})
    record("an unknown action is refused", not r.get("success"))

    ok, L = m.tool("stellaris_get_leaders")
    envoy = next((x for x in L.get("hired_leaders", []) if x["class"] == "envoy" and x["assignment_type"] == "unassigned"), None)
    if envoy:
        ok, r = m.tool("stellaris_assign_leader", {"leader_id": envoy["id"], "slot": "envoy", "task": "improve_relations", "target_id": EMPIRE})
        record("send an envoy to improve relations", ok, str(r)[:120])
        m.tool("stellaris_run_until", {"days": 1, "max_seconds": 20})
        ok, L = m.tool("stellaris_get_leaders")
        x = next(x for x in L["hired_leaders"] if x["id"] == envoy["id"])
        record("the envoy works on that empire", x["assignment_type"] == "empire" and x["target_id"] == EMPIRE, f"{x['assignment_type']} {x['target_id']}")
    else:
        print("[SKIP] no free envoy")
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
