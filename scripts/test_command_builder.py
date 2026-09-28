"""End-to-end check of CommandBuilder-dispatched commands against a live, in-game session.

Only reversible actions: it switches research in one area, cancels it, and restores the
original selection. Every step is verified by reading the research state back (PostCommand
silently drops commands that fail the engine's permission gate, so "posted" is not enough).

Usage: python scripts/test_command_builder.py
"""
import sys
import time

import test_pipe

AREAS = ("physics", "society", "engineering")


class Pipe:
    def __init__(self, f):
        self.f, self.n = f, 0

    def call(self, method, params=None):
        self.n += 1
        r = test_pipe.send_request(self.f, self.n, method, params or {})
        if "error" in r:
            raise RuntimeError(f"{method}: {r['error']}")
        res = r.get("result", r)
        if isinstance(res, dict) and "error" in res:
            raise RuntimeError(f"{method}: {res['error']}")
        return res


def current(p, area):
    st = p.call("get_research_state")
    cur = st[AREAS[area]]["current"]
    return cur["key"] if cur["is_researching"] else None, st[AREAS[area]]["candidates"]


def wait_for(p, area, want, timeout=6.0):
    end = time.time() + timeout
    got = None
    while time.time() < end:
        got, _ = current(p, area)
        if got == want:
            return True
        time.sleep(0.3)
    print(f"    expected {want!r}, still {got!r}")
    return False


def main():
    with open(test_pipe.PIPE_PATH, "r+b", buffering=0) as f:
        p = Pipe(f)
        if not p.call("get_status").get("in_game"):
            sys.exit("load a save first (get_status.in_game is false)")

        # pick an area that is researching something and offers an alternative
        for area in range(3):
            orig, cands = current(p, area)
            alts = [c["key"] for c in cands if c["key"] != orig]
            if orig and alts:
                break
        else:
            sys.exit("no area with an active tech and an alternative candidate")
        print(f"area {AREAS[area]}: original={orig} candidates={alts}")

        # Invalid candidates must be rejected with the engine's reason and must NOT cancel
        # the current research; the first valid one is used for the round trip.
        alt = None
        for key in alts:
            try:
                p.call("select_research", {"area": area, "tech_key": key})
                alt = key
                break
            except RuntimeError as e:
                still, _ = current(p, area)
                print(f"    rejected {key}: {e}")
                if still != orig:
                    sys.exit(f"[FAIL] rejected selection cancelled the current research ({orig!r} -> {still!r})")
        if alt is None:
            sys.exit("no candidate was accepted")
        print(f"[PASS] rejections kept {orig}; accepted {alt}")

        results = []

        def step(name, fn, want):
            try:
                fn()
                ok = wait_for(p, area, want)
            except Exception as e:  # noqa: BLE001 - report and keep restoring
                print(f"    {e}")
                ok = False
            results.append((name, ok))
            print(f"[{'PASS' if ok else 'FAIL'}] {name}")

        step("select_research -> alternative (already posted above)", lambda: None, alt)
        step("cancel_research",
             lambda: p.call("cancel_research", {"area": area}), None)
        step("select_research -> restore original",
             lambda: p.call("select_research", {"area": area, "tech_key": orig}), orig)

        if not all(ok for _, ok in results):
            final, _ = current(p, area)
            print(f"final state in {AREAS[area]}: {final!r} (original was {orig!r})")
            sys.exit(1)
        print("all command-builder checks passed")


if __name__ == "__main__":
    main()
