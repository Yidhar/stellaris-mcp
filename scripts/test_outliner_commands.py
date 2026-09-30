"""Live test of the outliner commands dispatched through CommandBuilder (disposable save).

Every case reads the game state back. Terraforming, workforce limits and army settings are
undone afterwards; enacting a decision and job priority advance the save.

Usage: python scripts/test_outliner_commands.py [planet_id]   (default 3)
"""
import json
import sys
import time

import test_pipe
from test_all_commands import Pipe, RESULTS, act, record


def main():
    planet = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    with open(test_pipe.PIPE_PATH, "r+b", buffering=0) as f:
        p = Pipe(f)
        if not p.raw("get_status")[0].get("in_game"):
            sys.exit("load a save first")

        # ---- terraforming: start -> verify -> cancel -> verify ---------------------
        tf = p.raw("get_terraforming_options", {"planet_id": planet})[0]
        opt = next((o for o in tf.get("options", []) if o.get("can_terraform")), None)
        if tf.get("is_terraforming"):
            record("start_terraforming", "SKIP", "planet already terraforming")
        elif not opt:
            record("start_terraforming", "SKIP", "no terraforming option allowed")
        else:
            def terraforming(want):
                return p.raw("get_terraforming_options", {"planet_id": planet})[0].get("is_terraforming") == want
            if act(p, f"start_terraforming -> {opt['target_planet_class']}", "start_terraforming",
                   {"planet_id": planet, "link_index": opt["link_index"], "target_class": opt["target_planet_class"]},
                   lambda: terraforming(True), expect_reject=True):
                act(p, "cancel_terraforming", "cancel_terraforming", {"planet_id": planet},
                    lambda: terraforming(False), expect_reject=True)
        res, err = p.raw("cancel_terraforming", {"planet_id": planet})
        record("cancel_terraforming when idle is refused", "PASS" if err or (res or {}).get("error") else "FAIL",
               json.dumps(res or err, ensure_ascii=False)[:120])

        # ---- jobs -------------------------------------------------------------------
        def jobs():
            out = {}
            for st in p.raw("get_planet_jobs", {"planet_id": planet})[0].get("strata", []):
                for j in st.get("jobs", []):
                    out[j["job_key"]] = j
            return out
        js = jobs()
        cand = next((j for j in js.values() if j.get("can_prioritize") and not j.get("is_prioritized")), None)
        if cand:
            act(p, f"set_job_priority ({cand['job_key']})", "set_job_priority",
                {"planet_id": planet, "job_key": cand["job_key"]},
                lambda: jobs().get(cand["job_key"], {}).get("is_prioritized"), expect_reject=True)
        else:
            record("set_job_priority", "SKIP", "no job to prioritize")

        lim = next((j for j in js.values() if j.get("max_workforce", 0) >= 200), None)
        if lim:
            key, orig = lim["job_key"], lim["workforce_limit"]
            target = orig - 100
            if act(p, f"set_job_workforce_limit {key} {orig}->{target}", "set_job_workforce_limit",
                   {"planet_id": planet, "job_key": key, "limit": target},
                   lambda: jobs().get(key, {}).get("workforce_limit") == target, expect_reject=True):
                act(p, f"set_job_workforce_limit {key} restore {orig}", "set_job_workforce_limit",
                    {"planet_id": planet, "job_key": key, "limit": orig},
                    lambda: jobs().get(key, {}).get("workforce_limit") == orig, expect_reject=True)
        else:
            record("set_job_workforce_limit", "SKIP", "no job with workforce")

        # ---- army settings: flip -> verify -> flip back ------------------------------
        def overview():
            return p.raw("get_planet_armies", {"planet_id": planet})[0].get("overview", {})
        ov = overview()
        for flag in ("deploy_in_orbit", "include_in_builder"):
            cur = ov.get(flag)
            if cur is None:
                record(f"set_planet_army_settings {flag}", "SKIP", "flag not reported")
                continue
            if act(p, f"set_planet_army_settings {flag} {cur}->{not cur}", "set_planet_army_settings",
                   {"planet_id": planet, flag: not cur}, lambda: overview().get(flag) == (not cur), expect_reject=True):
                act(p, f"set_planet_army_settings {flag} restore {cur}", "set_planet_army_settings",
                    {"planet_id": planet, flag: cur}, lambda: overview().get(flag) == cur, expect_reject=True)

        # ---- decision -----------------------------------------------------------------
        decs = p.raw("get_planetary_decisions", {"planet_id": planet})[0].get("decisions", [])
        dec = next((d for d in decs if d.get("can_enact")), None)
        if dec:
            def enacted():
                now = p.raw("get_planetary_decisions", {"planet_id": planet})[0].get("decisions", [])
                return not next((d for d in now if d["key"] == dec["key"]), {}).get("can_enact", True)
            act(p, f"enact_decision ({dec['key']})", "enact_decision", {"planet_id": planet, "decision_key": dec["key"]},
                enacted, expect_reject=True)
        else:
            record("enact_decision", "SKIP", "no enactable decision")
        blocked = next((d for d in decs if not d.get("can_enact")), None)
        if blocked:
            res, err = p.raw("enact_decision", {"planet_id": planet, "decision_key": blocked["key"]})
            refused = err or (res or {}).get("success") is False or (res or {}).get("error")
            record(f"enact_decision refused ({blocked['key']})", "PASS" if refused else "FAIL",
                   json.dumps(res or err, ensure_ascii=False)[:160])

        # ---- ascension ----------------------------------------------------------------
        asc = p.raw("get_planet_details", {"planet_id": planet})[0].get("colony_ascension", {})
        if asc.get("can_ascend"):
            act(p, "ascend_colony", "ascend_colony", {"planet_id": planet},
                lambda: p.raw("get_planet_details", {"planet_id": planet})[0]["colony_ascension"]["tier"] > asc["tier"],
                expect_reject=True)
        else:
            res, err = p.raw("ascend_colony", {"planet_id": planet})
            refused = err or (res or {}).get("success") is False or (res or {}).get("error")
            record("ascend_colony refused when not allowed", "PASS" if refused else "FAIL",
                   json.dumps(res or err, ensure_ascii=False)[:160])

        # ---- buildables (CAddBuildableToQueueCommand) ---------------------------------
        def queue_keys():
            q = p.raw("get_planet_details", {"planet_id": planet})[0].get("construction_queue", [])
            return [json.dumps(item, ensure_ascii=False) for item in q]
        before = queue_keys()
        zones = p.raw("get_buildable_buildings", {"planet_id": planet})[0].get("zones", [])
        pick = next(((z, z["buildable"][0]) for z in zones if z.get("buildable")), None)
        if pick:
            z, b0 = pick
            # the listed buildings are the ones the engine accepts, so this must queue
            act(p, f"build_building ({b0['key']} in zone {z['zone_id']})", "build_building",
                {"planet_id": planet, "building_key": b0["key"], "slot_index": z["zone_id"]},
                lambda: len(queue_keys()) > len(before) and any(b0["key"] in k for k in queue_keys()))
        else:
            record("build_building", "SKIP", "nothing buildable")

        det = p.raw("get_planet_details", {"planet_id": planet})[0]
        lvl1 = next((b for d in det.get("districts", []) for z in d.get("zones", []) for b in z.get("buildings", [])
                     if b.get("key", "").endswith("_1") and b.get("status") == "built"), None)
        if lvl1:
            target = lvl1["key"][:-2] + "_2"
            before = queue_keys()
            act(p, f"upgrade_building ({lvl1['key']} -> {target})", "upgrade_building",
                {"planet_id": planet, "building_id": lvl1["id"], "upgrade_to_key": target},
                lambda: len(queue_keys()) > len(before), expect_reject=True)
        else:
            record("upgrade_building", "SKIP", "no level-1 building")

        # clear a blocker on any colony that has one (probe + dispatch)
        sectors = p.raw("get_sectors")[0]
        colony_planets = sorted({int(x) for x in __import__("re").findall(r'"planet_id": (\d+)', json.dumps(sectors))} | {planet})
        found = None
        for pid in colony_planets:
            bl = p.raw("get_clearable_blockers", {"planet_id": pid})[0].get("blockers", [])
            clearable = [b for b in bl if b.get("can_clear") or b.get("status") == "可清理"]
            if clearable:
                found = (pid, clearable[0])
                break
        if found:
            pid, blk = found
            bq = lambda: p.raw("get_clearable_blockers", {"planet_id": pid})[0].get("blockers", [])
            act(p, f"clear_blocker (planet {pid}, {blk.get('key') or blk.get('deposit_key')})", "clear_blocker",
                {"planet_id": pid, "deposit_id": blk.get("deposit_id", blk.get("id"))},
                lambda: any(b.get("is_queued") or b.get("status") == "这个障碍已经在清除中了。" for b in bq()), expect_reject=True)
        else:
            record("clear_blocker", "SKIP", f"no clearable blocker on {len(colony_planets)} colonies")

        arm = p.raw("get_planet_armies", {"planet_id": planet})[0]
        rec = next(iter(arm.get("recruitable_armies", [])), None)
        if rec:
            n0 = len(arm.get("construction_queue", []))
            act(p, f"recruit_planet_army ({rec['key']})", "recruit_planet_army", {"planet_id": planet, "army_key": rec["key"]},
                lambda: len(p.raw("get_planet_armies", {"planet_id": planet})[0].get("construction_queue", [])) > n0,
                expect_reject=True)
        else:
            record("recruit_planet_army", "SKIP", "nothing recruitable")

        # ---- armies carrying a CPdxArray of army refs ------------------------------------
        def stationed():
            return p.raw("get_planet_armies", {"planet_id": planet})[0].get("stationed_armies", [])
        defense = [a for a in stationed() if a.get("is_defense")]
        if defense:
            victim = defense[-1]["army_id"]
            # CDisbandArmyCommand only marks the army SetToBeKilled; it disappears on the next
            # daily update, so let the game run for a moment before checking.
            def gone_after_tick():
                p.raw("set_paused", {"paused": False})
                time.sleep(2.5)
                p.raw("set_paused", {"paused": True})
                return all(a["army_id"] != victim for a in stationed())
            act(p, f"disband_planet_army ({victim})", "disband_planet_army", {"planet_id": planet, "army_id": victim},
                gone_after_tick, expect_reject=True)
        else:
            record("disband_planet_army", "SKIP", "no defense army")
        assault = [a for a in stationed() if not a.get("is_defense")]
        if assault:
            act(p, f"embark_all_armies ({len(assault)} assault armies)", "embark_all_armies", {"planet_id": planet},
                lambda: not any(not a.get("is_defense") for a in stationed()), expect_reject=True)
        else:
            record("embark_all_armies", "SKIP", "no assault army on planet")

        record("bridge alive", "PASS" if p.raw("ping")[0].get("status") == "pong" else "FAIL")

    fails = [r for r in RESULTS if r[1] == "FAIL"]
    print(f"\n{sum(r[1] == 'PASS' for r in RESULTS)} passed, {len(fails)} failed, "
          f"{sum(r[1] == 'SKIP' for r in RESULTS)} skipped")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
