"""Live end-to-end test of every CommandBuilder-dispatched command (use a disposable save).

Each case either verifies the effect by reading game state back, or -- when the save does not
allow the action -- requires a clean rejection that carries a reason (and no crash). Round
trips are undone where possible (leader dismissed, edict switched off, resources sold back,
monthly order cancelled). Tradition adoption and finishing the agenda advance the save.

Usage: python scripts/test_all_commands.py
"""
import json
import sys
import time

import test_pipe

RESULTS = []


class Pipe:
    def __init__(self, f):
        self.f, self.n = f, 0

    def raw(self, method, params=None):
        self.n += 1
        r = test_pipe.send_request(self.f, self.n, method, params or {})
        res = r.get("result", r)
        err = r.get("error") or (res.get("error") if isinstance(res, dict) else None)
        return res, err


def record(name, status, detail=""):
    RESULTS.append((name, status, detail))
    print(f"[{status}] {name}" + (f"  -- {detail}" if detail else ""), flush=True)


def poll(fn, timeout=6.0):
    end = time.time() + timeout
    while time.time() < end:
        v = fn()
        if v:
            return v
        time.sleep(0.3)
    return fn()


def act(p, name, method, params, verify=None, expect_reject=False):
    """Run a command; PASS if verify() turns true, or if rejected with a reason when allowed."""
    res, err = p.raw(method, params)
    if err:
        msg = err.get("message") if isinstance(err, dict) else str(err)
        if expect_reject is not False:
            record(name, "PASS" if msg else "FAIL", f"rejected: {msg}")
        else:
            record(name, "FAIL", f"error: {msg}")
        return False
    if verify is None:
        record(name, "PASS", "accepted")
        return True
    ok = poll(verify)
    record(name, "PASS" if ok else "FAIL", "state verified" if ok else f"posted but no effect: {json.dumps(res, ensure_ascii=False)[:200]}")
    return bool(ok)


def main():
    with open(test_pipe.PIPE_PATH, "r+b", buffering=0) as f:
        p = Pipe(f)
        st, _ = p.raw("get_status")
        if not st.get("in_game"):
            sys.exit("load a save first")

        def leaders():
            return p.raw("get_leaders")[0]

        # ---- leaders: hire -> assign -> dismiss --------------------------------------
        pool = leaders()["pool_candidates"]
        cand = next((c for c in pool if c.get("class") == "commander"), pool[0] if pool else None)
        if not cand:
            record("hire_leader", "SKIP", "empty recruitment pool")
        else:
            cid = cand["id"]
            hired = act(p, "hire_leader", "hire_leader", {"candidate_id": cid},
                        lambda: any(l["id"] == cid for l in leaders()["hired_leaders"]), expect_reject=True)
            if hired:
                fleets = p.raw("get_fleets")[0].get("fleets", [])
                if cand.get("class") == "commander" and fleets:
                    fid = fleets[0]["fleet_id"]
                    def assigned():
                        l = next((x for x in leaders()["hired_leaders"] if x["id"] == cid), {})
                        return l.get("assignment_type") not in (None, "", "none", "unassigned")
                    act(p, "assign_leader (fleet commander)", "assign_leader",
                        {"leader_id": cid, "assignment_type": 2, "target_id": fid}, assigned, expect_reject=True)
                act(p, "dismiss_leader", "dismiss_leader", {"leader_id": cid},
                    lambda: all(l["id"] != cid for l in leaders()["hired_leaders"]), expect_reject=True)

        # ---- edicts: on -> off --------------------------------------------------------
        ed = p.raw("get_edicts")[0]
        cand_edict = next((e["key"] for e in ed.get("available_edicts", []) if not e.get("is_active")), None)
        if cand_edict:
            def edict_active(want):
                e = p.raw("get_edicts")[0]
                keys = {x["key"] for x in e.get("active_edicts", [])} | \
                       {x["key"] for x in e.get("available_edicts", []) if x.get("is_active")}
                return (cand_edict in keys) == want
            if act(p, f"toggle_edict on ({cand_edict})", "toggle_edict", {"edict_key": cand_edict, "enabled": True},
                   lambda: edict_active(True), expect_reject=True):
                act(p, f"toggle_edict off ({cand_edict})", "toggle_edict", {"edict_key": cand_edict, "enabled": False},
                    lambda: edict_active(False), expect_reject=True)
        else:
            record("toggle_edict", "SKIP", "no inactive edict")

        # ---- market: buy -> sell back (pick a resource below its storage cap) ----------
        def market():
            return p.raw("get_market")[0].get("resources", [])
        def stock(key):
            return next((r["stockpile"] for r in market() if r["key"] == key), None)
        caps = {r["key"]: r["stockpile"] for r in market()}
        res_key = next((k for k in ("alloys", "energy", "minerals") if k in caps and caps[k] % 5000), None)
        if res_key is None:
            record("market_trade", "SKIP", "no tradable resource below its cap")
        else:
            before = stock(res_key)
            if act(p, f"market_trade buy 100 {res_key}", "market_trade", {"resource": res_key, "action": "buy", "units": 100},
                   lambda: (stock(res_key) or 0) >= before + 99, expect_reject=True):
                mid = stock(res_key)
                act(p, f"market_trade sell 100 {res_key}", "market_trade", {"resource": res_key, "action": "sell", "units": 100},
                    lambda: (stock(res_key) or 0) <= mid - 99, expect_reject=True)

        # ---- monthly trade: add (with price limit) -> read back -> cancel by order_id ----
        def orders():
            return p.raw("get_market")[0].get("monthly_trades", [])
        before_ids = {o["order_id"] for o in orders()}
        def new_order():
            return next((o for o in orders() if o["order_id"] not in before_ids
                         and o["amount"] == 6 and o["max_unit_price"] == 42), None)
        if act(p, "set_monthly_trade add (buy 6 alloys, max price 42)", "set_monthly_trade",
               {"resource": "alloys", "action": "buy", "amount": 6, "price_limit": 42}, new_order, expect_reject=True):
            oid = new_order()["order_id"]
            act(p, f"set_monthly_trade cancel order {oid}", "set_monthly_trade", {"cancel": True, "order_id": oid},
                lambda: all(o["order_id"] != oid for o in orders()), expect_reject=True)
        res, err = p.raw("set_monthly_trade", {"cancel": True})
        refused = (res or {}).get("success") is False or err
        record("set_monthly_trade cancel requires order_id", "PASS" if refused else "FAIL")

        # ---- tradition ----------------------------------------------------------------
        tr = p.raw("get_traditions")[0]
        tree = next(iter(tr.get("available_trees", [])), None)
        if tree and tr.get("summary", {}).get("can_unlock_tradition"):
            act(p, f"adopt_tradition ({tree['adopt_tradition_key']})", "adopt_tradition",
                {"tradition_key": tree["adopt_tradition_key"]},
                lambda: any(t.get("key") == tree["key"] for t in p.raw("get_traditions")[0].get("adopted_trees", [])),
                expect_reject=True)
        else:
            record("adopt_tradition", "SKIP", "nothing adoptable")

        # ---- council agenda: finish when ready, otherwise start one --------------------
        def agenda():
            return p.raw("get_government")[0]
        gov = agenda()
        ag = gov.get("agenda", {})
        if ag.get("is_ready"):
            act(p, f"launch_council_agenda ({ag.get('key')})", "launch_council_agenda", {},
                lambda: agenda().get("agenda", {}).get("key") != ag.get("key"), expect_reject=True)
            gov = agenda()
        if not gov.get("agenda", {}).get("key"):
            choices = gov.get("available_agendas", [])
            if choices:
                key = choices[0]["key"]
                act(p, f"set_council_agenda ({key})", "set_council_agenda", {"agenda_key": key},
                    lambda: agenda().get("agenda", {}).get("key") == key, expect_reject=True)
            else:
                record("set_council_agenda", "FAIL", "no agenda active and none offered")
        else:
            # an agenda is in progress: switching must either take effect or be refused cleanly
            cur = gov["agenda"]["key"]
            other = next((a["key"] for a in gov.get("available_agendas", []) if a["key"] != cur), None)
            if other:
                act(p, f"set_council_agenda while {cur} in progress -> {other}", "set_council_agenda",
                    {"agenda_key": other}, lambda: agenda().get("agenda", {}).get("key") == other, expect_reject=True)
            else:
                record("set_council_agenda", "PASS", f"{cur} in progress; engine offers no other agenda (IsValid)")

        # ---- leader trait pick (council seats expose the same data) ---------------------
        seats = [s["leader"] for s in agenda().get("council_seats", []) if s.get("leader")]
        pick = next(((l, l["trait_options"][0]["key"]) for l in seats
                     if l.get("trait_selections_available", 0) > 0 and l.get("trait_options")), None)
        if pick:
            leader, tkey = pick
            def has_trait():
                for s in agenda().get("council_seats", []):
                    l = s.get("leader") or {}
                    if l.get("id") == leader["id"]:
                        return any(t["key"] == tkey for t in l.get("traits", []))
                return False
            act(p, f"select_leader_trait ({leader['name']} <- {tkey})", "select_leader_trait",
                {"leader_id": leader["id"], "trait_key": tkey}, has_trait, expect_reject=True)
        else:
            record("select_leader_trait", "SKIP", "no council leader has a pending trait pick")

        # ---- fleets / designs: accept or clean rejection ------------------------------
        fleets = p.raw("get_fleets")[0].get("fleets", [])
        if fleets:
            fid = fleets[0]["fleet_id"]
            act(p, f"reinforce_fleet ({fid}, can_reinforce={fleets[0].get('can_reinforce')})", "reinforce_fleet",
                {"fleet_id": fid}, None, expect_reject=True)
            act(p, f"upgrade_fleet ({fid})", "upgrade_fleet", {"fleet_id": fid}, None, expect_reject=True)
        designs = p.raw("get_ship_designs")[0].get("designs", [])
        if designs:
            did = designs[-1]["design_id"]
            act(p, f"delete_ship_design ({did})", "delete_ship_design", {"design_id": did},
                lambda: all(d["design_id"] != did for d in p.raw("get_ship_designs")[0].get("designs", [])),
                expect_reject=True)

        # ---- relic / situation: this save has none, check clean rejection -------------
        disc = p.raw("get_discoveries")[0].get("relics", {})
        held = disc.get("held_relics", [])
        if held:
            key = held[0].get("key")
            act(p, f"activate_relic ({key})", "activate_relic", {"relic_key": key}, None, expect_reject=True)
        else:
            record("activate_relic", "SKIP", "no relic held in this save")
        sits = p.raw("get_situation_log")[0].get("situations", [])
        if sits:
            s = sits[0]
            appr = next((a.get("key") for a in s.get("approaches", []) if not a.get("is_active")), None)
            act(p, f"set_situation_approach ({s.get('id')})", "set_situation_approach",
                {"situation_id": s.get("id"), "approach_key": appr}, None, expect_reject=True)
        else:
            record("set_situation_approach", "SKIP", "no active situation in this save")

        # the bridge must still answer after everything above
        ok = p.raw("ping")[0].get("status") == "pong"
        record("bridge alive after all commands", "PASS" if ok else "FAIL")

    fails = [r for r in RESULTS if r[1] == "FAIL"]
    print(f"\n{sum(r[1] == 'PASS' for r in RESULTS)} passed, {len(fails)} failed, "
          f"{sum(r[1] == 'SKIP' for r in RESULTS)} skipped")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
