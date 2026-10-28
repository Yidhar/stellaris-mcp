"""End-to-end check of creating, editing and deleting empire designs through the built MCP server
(stdio): validate-only leaves everything as it was, saving a new design from a prescripted base,
editing it in place, the game refusing an invalid one, the refusals, deleting it, and the user design
file around it (the other designs' text never changes).

Needs the game on the main menu with the bridge injected. Works on a design keyed mcp_test_design;
the user design file is put back as it was at the end (also on failure), with --keep to keep the
design for a check after a game restart (then delete it with --delete-only).

    python scripts/test_mcp_empire_design_edit_end_to_end.py [--keep | --delete-only]
"""
import argparse
import sys
from pathlib import Path

from test_mcp_session_end_to_end import Mcp, RESULTS, record

KEY = "mcp_test_design"


def run(m, args):
    ok, scr = m.tool("stellaris_get_status")
    if not ok or scr.get("screen") != "main_menu":
        print(f"needs the main menu, the game is on {scr.get('screen') if ok else scr}")
        return 1
    ok, lst = m.tool("stellaris_list_empire_designs")
    count = lst["count"]
    ok, t = m.tool("stellaris_get_empire_design_text", {"key": "humans1_1"})
    record("design text of a prescripted empire", ok and t["text"].startswith("humans1_1") and "prescripted_countries" in t["file"], str(t)[:120])

    if args.delete_only:
        ok, r = m.tool("stellaris_delete_empire_design", {"key": KEY})
        record("delete the kept design", ok and r.get("deleted") == KEY and r.get("removed_from_file"), str(r)[:160])
        return 0

    changes = {"name": "MCP 测试联邦", "species_name": "测试人", "ethics": ["ethic_fanatic_materialist", "ethic_egalitarian"]}
    ok, r = m.tool("stellaris_save_empire_design", {"key": KEY, "base": "humans1_1", "changes": changes, "validate_only": True})
    record("validate_only: the game's validation, not saved", ok and r.get("valid") is True and r.get("saved") is False, str(r)[:200])
    ok, lst = m.tool("stellaris_list_empire_designs")
    record("validate_only leaves the design list", ok and lst["count"] == count and KEY not in {d["key"] for d in lst["designs"]})

    ok, r = m.tool("stellaris_save_empire_design", {"key": KEY, "base": "humans1_1", "changes": changes})
    record("save a new design from a prescripted base", ok and r.get("saved") is True and r.get("valid") is True, str(r)[:200])
    if not ok:
        return 1
    user_file = Path(r["file"])
    ok, d = m.tool("stellaris_get_empire_design", {"key": KEY})
    record("the game has it as a user design with the changes", ok and d.get("type") == "user" and d.get("name") == "MCP 测试联邦"
           and {e["key"] for e in d.get("ethics", [])} == {"ethic_fanatic_materialist", "ethic_egalitarian"}
           and d["species"]["name"] == "测试人" and d.get("valid") is True, str(d)[:200])
    record("the user design file has it", f'"{KEY}"' in user_file.read_text(encoding="utf-8"))

    ok, r = m.tool("stellaris_save_empire_design", {"key": KEY, "base": "humans1_1", "changes": changes})
    record("an existing key without overwrite is refused", not ok and "overwrite" in str(r), str(r)[:120])
    ok, r = m.tool("stellaris_save_empire_design", {"key": "humans1_1", "base": "humans1_1"})
    record("a prescripted key is refused", not ok and "prescripted" in str(r), str(r)[:120])

    ok, r = m.tool("stellaris_save_empire_design", {"key": KEY, "changes": {"name": "MCP 改名联邦", "civics": ["civic_meritocracy", "civic_idealistic_foundation"]}})
    record("edit in place", ok and r.get("saved") is True and r.get("replaced") is True, str(r)[:200])
    ok, d = m.tool("stellaris_get_empire_design", {"key": KEY})
    record("the edit is in the game", ok and d.get("name") == "MCP 改名联邦" and {c["key"] for c in d["civics"]} == {"civic_meritocracy", "civic_idealistic_foundation"}, str(d.get("civics"))[:160])
    text = user_file.read_text(encoding="utf-8")
    record("the file holds the design once, edited", text.count(f'"{KEY}" =') == 1 and "MCP 改名联邦" in text and "MCP 测试联邦" not in text)
    ok, lst = m.tool("stellaris_list_empire_designs")
    record("one more design in the list", ok and lst["count"] == count + 1)

    before = user_file.read_bytes()
    ok, r = m.tool("stellaris_save_empire_design", {"key": KEY, "changes": {"ethics": ["ethic_fanatic_egalitarian", "ethic_authoritarian"]}})
    record("an invalid design is refused with the game's reason", ok and r.get("valid") is False and r.get("saved") is False and r.get("invalid_reason"),
           str(r.get("invalid_reason"))[:160])
    ok, d = m.tool("stellaris_get_empire_design", {"key": KEY})
    record("the refused edit changed nothing", ok and d.get("name") == "MCP 改名联邦" and user_file.read_bytes() == before)

    ok, r = m.tool("stellaris_save_empire_design", {"key": "mcp_test_broken", "text": "mcp_test_broken = { name = "})
    record("broken script refused", not ok, str(r)[:120])

    if args.keep:
        print(f"kept {KEY}: restart the game, check it, then --delete-only")
        return 0
    ok, r = m.tool("stellaris_delete_empire_design", {"key": KEY})
    record("delete", ok and r.get("deleted") == KEY and r.get("removed_from_file"), str(r)[:160])
    ok, lst = m.tool("stellaris_list_empire_designs")
    record("gone from the list", ok and lst["count"] == count and KEY not in {d["key"] for d in lst["designs"]})
    ok, r = m.tool("stellaris_delete_empire_design", {"key": "humans1_1"})
    record("a prescripted empire cannot be deleted", not ok, str(r)[:120])
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--delete-only", action="store_true")
    args = ap.parse_args()
    m = Mcp()
    # the user design files (the game writes the newest name), kept to put back afterwards
    user_dir = Path(m.tool("stellaris_list_saves")[1]["saves_folder"]).parent
    saved = {f: f.read_bytes() for f in user_dir.glob("user_empire_designs*.txt")}
    rc = 1
    try:
        rc = run(m, args)
        if not args.keep and not args.delete_only:
            record("the user design files are byte for byte as before", all(f.read_bytes() == b for f, b in saved.items()))
    finally:
        if not args.keep and not args.delete_only:
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
