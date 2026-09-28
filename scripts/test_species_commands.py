"""Live test of the species commands (use a disposable save).

Rights: a disallowed right must be refused with the engine's trigger text, a category on
cooldown must be refused, and an allowed change (if any exists) must read back. Templates:
create -> modify (the engine re-creates the template under a new id) -> delete round trip.
Apply: either starts the project or is refused with the engine's reason.

Usage: python scripts/test_species_commands.py
"""
import sys

import test_pipe
from test_all_commands import Pipe, RESULTS, act, poll, record


def main():
    with open(test_pipe.PIPE_PATH, "r+b", buffering=0) as f:
        p = Pipe(f)
        if not p.raw("get_status")[0].get("in_game"):
            sys.exit("load a save first")

        def species():
            return p.raw("get_species")[0]

        def templates():
            return {s["species_id"]: s for s in species()["species"] if s["is_template"]}

        sp = species()
        founder = next(s for s in sp["species"] if s["is_founder"])
        sid, rights, catalog = founder["species_id"], founder["rights"], sp["available_rights_catalog"]

        # ---- rights ---------------------------------------------------------------------
        act(p, "set_species_rights founder -> citizenship_slavery refused", "set_species_rights",
            {"species_id": sid, "category": "citizenship", "right_value": "citizenship_slavery"},
            expect_reject=True)
        changed = False
        for cat in ("living_standards", "military_service", "migration_controls", "colonization_controls",
                    "population_controls", "purge_type", "slavery_type"):
            cur = rights[cat]["key"]
            for opt in catalog.get(cat, []):
                if opt["key"] == cur:
                    continue
                res, err = p.raw("set_species_rights", {"species_id": sid, "category": cat, "right_value": opt["key"]})
                if err and "cooldown" in str(err):
                    record(f"set_species_rights {cat} on cooldown refused", "PASS", str(err)[:120])
                    break
                if err:
                    continue
                ok = poll(lambda: next(s for s in species()["species"] if s["species_id"] == sid)["rights"][cat]["key"] == opt["key"])
                record(f"set_species_rights {cat} {cur} -> {opt['key']}", "PASS" if ok else "FAIL",
                       "state verified" if ok else "posted but no effect")
                changed = True
                break
            if changed:
                break
        if not changed:
            record("set_species_rights allowed change", "SKIP", "every alternative is disallowed or on cooldown")

        # ---- templates: create -> modify -> delete -----------------------------------------
        base_traits = [t["key"] for t in founder["traits"]]
        before = templates()
        act(p, "create_species_template", "create_species_template",
            {"base_species_id": sid, "name": "MCP Test", "traits": base_traits},
            lambda: [k for k in templates() if k not in before])
        new = [k for k in templates() if k not in before]
        if new:
            tid = new[0]
            before = templates()
            renamed = lambda: [k for k, s in templates().items() if s["name"] == "MCP Test 2"]
            act(p, "modify_species_template (rename)", "modify_species_template",
                {"template_species_id": tid, "name": "MCP Test 2", "traits": base_traits}, renamed)
            tid = (renamed() or [tid])[0]
            act(p, "delete_species_template", "delete_species_template", {"species_id": tid},
                lambda: tid not in templates())
        else:
            record("modify/delete_species_template", "SKIP", "template was not created")

        # ---- apply: start the project or be refused with the engine's reason ---------------
        tmpl = next(iter(templates()), None)
        if tmpl is None:
            record("apply_species_template", "SKIP", "no template")
        else:
            act(p, f"apply_species_template ({tmpl})", "apply_species_template",
                {"template_species_id": tmpl}, None, expect_reject=True)

        ok = p.raw("ping")[0].get("status") == "pong"
        record("bridge alive", "PASS" if ok else "FAIL")

    fails = [r for r in RESULTS if r[1] == "FAIL"]
    print(f"\n{sum(r[1] == 'PASS' for r in RESULTS)} passed, {len(fails)} failed, "
          f"{sum(r[1] == 'SKIP' for r in RESULTS)} skipped")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
