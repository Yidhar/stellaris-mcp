"""End-to-end check of what surfaces the events waiting for the player, through the built MCP server
(stdio): the status' pause_reason / pending_events, stellaris_run_until (stops at the date or at the
first event window, with the game paused), the [attention] note on other tools' results while an
event waits, and the note going away once it is resolved.

Needs the game running with the bridge injected. Loads the test campaign's save (TEST_CAMPAIGN),
lets up to a few game years pass to meet an event, and answers it with its first option.

    python scripts/test_mcp_attention_end_to_end.py
"""
import sys

from test_mcp_session_end_to_end import Mcp, RESULTS, TEST_CAMPAIGN, record


def run(m):
    ok, saves = m.tool("stellaris_list_saves", {"campaign": TEST_CAMPAIGN})
    first = next(s["file"] for s in saves["saves"] if s["file"] != "mcp_test_save.sav")
    ok, r = m.tool("stellaris_load_game", {"campaign": TEST_CAMPAIGN, "file": first})
    record("test save loaded", ok and r.get("screen") == "in_game", str(r)[:120])
    if not ok:
        return 1

    ok, st = m.tool("stellaris_get_status")
    record("status carries pending_events and, paused, pause_reason", ok and isinstance(st.get("pending_events"), list)
           and (not st.get("is_paused") or st.get("pause_reason") in ("event", "popup", "player")),
           str({k: st.get(k) for k in ("is_paused", "pause_reason", "pending_events")})[:160])

    ok, r = m.tool("stellaris_run_until", {"until_date": "2200-1-1"})
    record("run_until refuses a malformed date", not ok, str(r)[:100])

    ok, r = m.tool("stellaris_run_until", {"days": 30, "speed": 4})
    record("run_until stops at the date or an event, game paused", ok and r.get("stopped_because") in ("date", "event") and r.get("paused") is True
           and (r["stopped_because"] != "date" or r.get("days_passed") == 30), str(r)[:200])

    event = r.get("pending_events") if ok else None
    for _ in range(6):
        if event:
            break
        ok, r = m.tool("stellaris_run_until", {"days": 360, "speed": 4, "max_seconds": 90})
        if not ok:
            break
        event = r.get("pending_events")
    if not event:
        print("[SKIP] no event in the years run; the event checks need one")
        return 0
    record("run_until stops at an event window", r.get("stopped_because") == "event" and r.get("paused") is True,
           f"{r.get('date')}: {event}")
    record("pending event compact: window_id, kind, title", all("window_id" in e and "kind" in e for e in event)
           and any(e.get("title") for e in event if e.get("kind") == "event") or all(e.get("kind") != "event" for e in event))

    res = m.rpc("tools/call", {"name": "stellaris_get_market", "arguments": {}})["result"]
    notes = [c["text"] for c in res["content"][1:] if c["text"].startswith("[attention]")]
    record("another tool's result notes the waiting event", len(notes) == 1 and str(event[0]["window_id"]) in notes[0], notes[0][:160] if notes else "none")

    ok, r = m.tool("stellaris_run_until", {"days": 30})
    record("run_until does not start while events wait", ok and r.get("stopped_because") == "pending_events", str(r)[:120])

    ok, active = m.tool("stellaris_get_active_events")
    record("the same windows in get_active_events", ok and {e["window_id"] for e in active} == {e["window_id"] for e in event})
    for e in active if ok else []:
        m.tool("stellaris_resolve_event", {"window_id": e["window_id"], "option_index": e["options"][0]["index"] if e.get("options") else 0})
    # an answer can open the chain's next event: then that one is noted, the answered ones are not
    ok, st = m.tool("stellaris_get_status")
    now = {e["window_id"] for e in st.get("pending_events", [])} if ok else set()
    res = m.rpc("tools/call", {"name": "stellaris_get_market", "arguments": {}})["result"]
    notes = [c["text"] for c in res["content"][1:] if c["text"].startswith("[attention]")]
    answered = {e["window_id"] for e in event}
    record("answered windows are no longer noted (a follow-up event is)", not (now & answered) and (bool(notes) == bool(now)),
           f"waiting now {sorted(now)}, note {'yes' if notes else 'no'}")
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
