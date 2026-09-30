"""Live test of the civic tools (use a disposable save).

Reads civics and civic points, checks that invalid requests are refused with a reason (unknown
civic, too many civic points, a civic the game rejects), and -- when a point is free and the game
accepts one -- adopts the first available civic and reads it back. Adopting starts the game's
20-year government reform cooldown.

Usage: python scripts/test_civics_pipeline.py
"""
import sys

import test_pipe
from test_all_commands import Pipe, RESULTS, act, record


def main():
    with open(test_pipe.PIPE_PATH, "r+b", buffering=0) as f:
        p = Pipe(f)
        if not p.raw("get_status")[0].get("in_game"):
            sys.exit("load a save first")

        civ, err = p.raw("get_civics")
        pts = civ.get("civic_points", {})
        ok = not err and pts.get("used") == sum(c["cost"] for c in civ["civics"]) and \
            pts.get("free") == pts.get("total", 0) - pts.get("used", 0)
        record("get_civics points consistent", "PASS" if ok else "FAIL", str(pts))

        act(p, "change_civics unknown civic refused", "change_civics", {"add": ["civic_does_not_exist"]},
            expect_reject=True)
        too_many = [a["key"] for a in civ.get("available_civics", [])][: pts.get("free", 0) + 1]
        if len(too_many) > pts.get("free", 0):
            act(p, "change_civics over civic points refused", "change_civics", {"add": too_many}, expect_reject=True)
        else:
            record("change_civics over civic points refused", "SKIP", "not enough candidate civics")

        adopted = {c["key"] for c in civ["civics"]}
        avail = civ.get("available_civics", [])
        if avail:
            key = avail[0]["key"]
            act(p, f"change_civics adopt {key}", "change_civics", {"add": [key]},
                lambda: key in {c["key"] for c in p.raw("get_civics")[0]["civics"]})
        else:
            record("change_civics adopt", "SKIP", f"no free civic point or no civic accepted now ({pts})")
            one = next(iter(adopted), None)
            if one:
                act(p, "change_civics while not allowed refused", "change_civics",
                    {"remove": [one], "add": ["civic_technocracy"]}, expect_reject=True)

        ok = p.raw("ping")[0].get("status") == "pong"
        record("bridge alive", "PASS" if ok else "FAIL")

    fails = [r for r in RESULTS if r[1] == "FAIL"]
    print(f"\n{sum(r[1] == 'PASS' for r in RESULTS)} passed, {len(fails)} failed, "
          f"{sum(r[1] == 'SKIP' for r in RESULTS)} skipped")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
