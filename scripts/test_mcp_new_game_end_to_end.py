"""End-to-end check of the new-game tools through the built MCP server (stdio): the options, galaxy
settings (and the galaxy size's limits), and starting new games on a tiny galaxy with a random
empire, a prescripted design, and a whole empire given inline (from the game's defaults), back to
the main menu after each.

Needs the game on the main menu with the bridge injected. The galaxy settings and settings.txt are
put back as they were at the end, and the inline test design (mcp_test_new_empire) is deleted.

    python scripts/test_mcp_new_game_end_to_end.py
"""
import sys
from pathlib import Path

from test_mcp_session_end_to_end import Mcp, RESULTS, record

DESIGN = "mcp_test_new_empire"
INLINE = {
    "key": DESIGN,
    "name": "MCP 测试共和国", "adjective": "测试",
    "species_class": "HUM", "portrait": "human", "name_list": "HUMAN1",
    "species_name": "测试人", "species_plural": "测试人", "species_adjective": "测试",
    "traits": ["trait_organic", "trait_intelligent", "trait_nomadic", "trait_wasteful"],
    "ethics": ["ethic_fanatic_egalitarian", "ethic_xenophile"],
    "authority": "auth_democratic", "government": "gov_representative_democracy",
    "civics": ["civic_beacon_of_liberty", "civic_idealistic_foundation"],
    "ruler_name": "测试总统", "ruler_gender": "female", "ruler_portrait": "human_female_05",
    "origin": "origin_default", "planet_class": "pc_continental", "planet_name": "测试星", "system_name": "测试系",
    "ship_set": "humanoid_01", "city_set": "humanoid_01", "ship_prefix": "MCP",
}


def back_to_menu(m):
    ok, r = m.tool("stellaris_exit_to_main_menu")
    record("back to the main menu", ok and r.get("screen") == "main_menu", str(r)[:120])
    return ok


def run(m):
    ok, scr = m.tool("stellaris_get_status")
    if not ok or scr.get("screen") != "main_menu":
        print(f"needs the main menu, the game is on {scr.get('screen') if ok else scr}")
        return 1
    ok, o = m.tool("stellaris_get_new_game_options")
    record("options: sizes, shapes, settings, choices", ok and len(o["sizes"]) >= 5 and o["shapes"] and "difficulty" in o["choices"]
           and "template" in o["settings"], f"{[s['template'] for s in o.get('sizes', [])]} shapes={len(o.get('shapes', []))}")
    if not ok:
        return 1
    original = o["settings"]
    tiny = next(s for s in o["sizes"] if s["template"] == "tiny")
    try:
        ok, r = m.tool("stellaris_set_galaxy_settings", {"settings": {"template": "tiny", "num_empires": tiny["empires"]["max"] + 5}})
        record("galaxy size's empire limit enforced", not ok and "num_empires" in str(r), str(r)[:120])
        ok, r = m.tool("stellaris_set_galaxy_settings", {"settings": {"difficulty": "emperor"}})
        record("unknown choice refused with the choices", not ok and "grand_admiral" in str(r), str(r)[:120])
        ok, r = m.tool("stellaris_set_galaxy_settings", {"settings": {"template": "no_such_size"}})
        record("unknown size refused", not ok, str(r)[:120])

        want = {"template": "tiny", "num_empires": 2, "num_fallen_empires": 0, "num_marauder_empires": 0, "difficulty": "cadet",
                "colonizable_planet_odds": 1.5, "crisis_type": "unbidden", "technology_difficulty_scale": "normal", "ironman": False}
        ok, r = m.tool("stellaris_set_galaxy_settings", {"settings": want})
        got = r.get("settings", {}) if ok else {}
        record("galaxy settings written", ok and all(got.get(k) == v for k, v in want.items()),
               str({k: got.get(k) for k in want})[:200])

        ok, r = m.tool("stellaris_new_game", {"empire": "random"})
        record("new game, random empire", ok and r.get("screen") == "in_game" and r.get("date", "").startswith("2200"), str(r)[:200])
        if ok:
            ok2, st = m.tool("stellaris_get_status")
            record("the new game is paused", ok2 and st.get("is_paused") is True)
            back_to_menu(m)

        ok, r = m.tool("stellaris_new_game", {"empire": "humans1_1"})
        record("new game as a prescripted empire", ok and r.get("screen") == "in_game", str(r)[:200])
        if ok:
            p = r.get("playing") or {}
            record("playing that empire (its ethics and civics)", set(p.get("ethics", [])) == {"ethic_fanatic_egalitarian", "ethic_xenophile"}
                   and set(p.get("civics", [])) == {"civic_beacon_of_liberty", "civic_idealistic_foundation"}, str(p)[:200])
            back_to_menu(m)

        bad = dict(INLINE, ethics=["ethic_fanatic_egalitarian", "ethic_authoritarian"])
        ok, r = m.tool("stellaris_new_game", {"empire": bad})
        record("an inline empire the game refuses: no start, the game's reason", not ok and "refuses" in str(r), str(r)[:160])
        ok, st = m.tool("stellaris_get_status")
        record("still on the main menu", ok and st.get("screen") == "main_menu")

        ok, r = m.tool("stellaris_new_game", {"empire": INLINE, "galaxy": {"num_empires": 1}})
        record("new game with a whole empire inline, from the defaults", ok and r.get("screen") == "in_game", str(r)[:240])
        if ok:
            p = r.get("playing") or {}
            record("playing the inline empire", set(p.get("ethics", [])) == set(INLINE["ethics"]) and set(p.get("civics", [])) == set(INLINE["civics"])
                   and p.get("species") == INLINE["species_name"] and p.get("ruler") == INLINE["ruler_name"], str(p)[:200])
            back_to_menu(m)
    finally:
        restore = {k: original.get(k) for k in original}
        ok, r = m.tool("stellaris_set_galaxy_settings", {"settings": restore})
        record("galaxy settings put back", ok and all(r["settings"].get(k) == v for k, v in original.items()), str(r)[:120])
        ok, r = m.tool("stellaris_delete_empire_design", {"key": DESIGN})
    return 0


def main():
    m = Mcp()
    user_dir = Path(m.tool("stellaris_list_saves")[1]["saves_folder"]).parent
    files = [user_dir / "settings.txt", *user_dir.glob("user_empire_designs*.txt")]
    saved = {f: f.read_bytes() for f in files if f.exists()}
    rc = 1
    try:
        rc = run(m)
    finally:
        for f, b in saved.items():
            f.write_bytes(b)
        for f in user_dir.glob("user_empire_designs*.txt.mcp_backup"):
            f.unlink()
        m.close()
    fails = [x for x in RESULTS if x[1] == "FAIL"]
    print(f"\n{len(RESULTS) - len(fails)} passed, {len(fails)} failed")
    return 1 if fails or rc else 0


if __name__ == "__main__":
    sys.exit(main())
