"""End-to-end check of the empire design tools through the built MCP server (stdio): the list, one
design in full against its file (humans1_1 in prescripted_countries\\00_top_countries.txt), the
game's validation on the main menu, and the refusal of an unknown key.

Needs the game on the main menu with the bridge injected. Reads only; changes nothing.

    python scripts/test_mcp_empire_design_end_to_end.py
"""
import sys

from test_mcp_session_end_to_end import Mcp, RESULTS, record


def run(m):
    ok, scr = m.tool("stellaris_get_status")
    if not ok or scr.get("screen") != "main_menu":
        print(f"needs the main menu, the game is on {scr.get('screen') if ok else scr}")
        return 1

    ok, lst = m.tool("stellaris_list_empire_designs")
    designs = lst.get("designs", []) if ok else []
    record("list returns designs with a count", ok and designs and lst.get("count") == len(designs), f"{len(designs)}")
    kinds = {d["type"] for d in designs}
    record("list has prescripted and user designs", {"prescripted", "user"} <= kinds or kinds == {"prescripted"}, str(sorted(kinds)))
    record("each summary is compact (no traits / civics)", all("traits" not in d and "civics" not in d for d in designs))
    by_key = {d["key"]: d for d in designs}
    h1, h11 = by_key.get("humans1"), by_key.get("humans1_1")
    record("humans1 is not playable, humans1_1 is (as in the file)", h1 and h11 and not h1["playable"] and h11["playable"],
           f"{h1 and h1['playable']} / {h11 and h11['playable']}")
    record("every design validated on the main menu", all(isinstance(d.get("valid"), bool) for d in designs),
           f"{sum(1 for d in designs if d.get('valid'))} valid of {len(designs)}")
    record("humans1_1 valid in the list", h11 and h11.get("valid") is True)

    ok, d = m.tool("stellaris_get_empire_design", {"key": "humans1_1"})
    record("detail of humans1_1", ok and d.get("key") == "humans1_1", str(d)[:160])
    if ok:
        sp = d.get("species", {})
        keys = lambda xs: {x.get("key") for x in xs}
        record("species class HUM", (sp.get("class") or {}).get("key") == "HUM", str(sp.get("class")))
        record("traits from the file", keys(sp.get("traits", [])) >= {"trait_adaptive", "trait_nomadic", "trait_wasteful"},
               str(keys(sp.get("traits", []))))
        record("ethics from the file", keys(d.get("ethics", [])) == {"ethic_xenophile", "ethic_fanatic_egalitarian"},
               str(keys(d.get("ethics", []))))
        record("civics from the file", keys(d.get("civics", [])) == {"civic_beacon_of_liberty", "civic_idealistic_foundation"},
               str(keys(d.get("civics", []))))
        record("authority, government, origin", (d.get("authority", {}).get("key"), d.get("government", {}).get("key"),
               d.get("origin", {}).get("key")) == ("auth_democratic", "gov_representative_democracy", "origin_default"))
        record("home system", d.get("home", {}).get("initializer") == "sol_system_initializer", str(d.get("home"))[:120])
        record("detail validated: valid", d.get("valid") is True, str(d.get("invalid_reason")))

    bad = next((k for k, x in by_key.items() if x.get("valid") is False), None)
    if bad:
        ok, d = m.tool("stellaris_get_empire_design", {"key": bad})
        record("an invalid design gives the game's reason", ok and d.get("valid") is False and d.get("invalid_reason"),
               f"{bad}: {str(d.get('invalid_reason'))[:120]}")

    ok, r = m.tool("stellaris_get_empire_design", {"key": "no_such_design_key"})
    record("unknown key refused", not ok, str(r)[:120])
    fails = [x for x in RESULTS if x[1] == "FAIL"]
    print(f"\n{len(RESULTS) - len(fails)} passed, {len(fails)} failed")
    return 1 if fails else 0


def main():
    m = Mcp()
    try:
        return run(m)
    finally:
        m.close()


if __name__ == "__main__":
    sys.exit(main())
