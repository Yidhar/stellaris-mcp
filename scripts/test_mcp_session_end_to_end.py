"""End-to-end check of the session tools through the built MCP server (stdio): list saves, load a
save from the main menu, save, leave for the main menu, load the new save back, and the refusals.

Needs the game running with the bridge injected. It loads saves of the campaign given with
--campaign (default: TEST_CAMPAIGN, the test save), writes one save named
mcp_test_save into it, and puts continue_game.json (the main menu's Continue target, which every
save rewrites) back as it was.

    python scripts/test_mcp_session_end_to_end.py [--campaign <folder>] [--file <save>]
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

RESULTS = []
# the test save: a 4.5 campaign (the game refuses saves of older versions)
TEST_CAMPAIGN = "10_689234343"
SERVER = Path(__file__).resolve().parent.parent / "stellaris_mcp_server" / "dist" / "index.js"

def record(name, ok, detail=""):
    status = "PASS" if ok else "FAIL"
    RESULTS.append((name, status))
    print(f"[{status}] {name}" + (f"  -- {detail}" if detail else ""))


class Mcp:
    def __init__(self):
        self.p = subprocess.Popen(["node", str(SERVER)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, encoding="utf-8", bufsize=1)
        self.n = 0
        self.rpc("initialize", {"protocolVersion": "2024-11-05", "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}})
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
        self.p.stdin.flush()

    def rpc(self, method, params):
        self.n += 1
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": self.n, "method": method, "params": params}) + "\n")
        self.p.stdin.flush()
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("MCP server closed")
            msg = json.loads(line)
            if msg.get("id") == self.n:
                return msg

    def tool(self, name, args=None):
        r = self.rpc("tools/call", {"name": name, "arguments": args or {}})["result"]
        text = r["content"][0]["text"]
        try:
            body = json.loads(text)
        except json.JSONDecodeError:
            body = text
        return (not r.get("isError")), body

    def close(self):
        self.p.terminate()


def run(m):
    ap = argparse.ArgumentParser()
    ap.add_argument("--campaign", default=TEST_CAMPAIGN)
    ap.add_argument("--file", default=None)
    args = ap.parse_args()
    ok, listing = m.tool("stellaris_list_saves")
    record("list_saves lists campaigns", ok and isinstance(listing, dict) and listing.get("campaigns"),
           f"{len(listing.get('campaigns', [])) if ok else listing}")
    if not ok:
        return 1
    user_dir = Path(listing["saves_folder"]).parent
    campaign = args.campaign
    ok, saves = m.tool("stellaris_list_saves", {"campaign": campaign})
    record("list_saves lists one campaign's saves with headers", ok and saves["saves"] and saves["saves"][0].get("date"),
           f"{campaign}: {len(saves['saves']) if ok else saves}")
    if not ok:
        return 1
    first = args.file or next((s["file"] for s in saves["saves"] if s["file"] != "mcp_test_save.sav"), saves["saves"][0]["file"])
    want_date = next(s.get("date") for s in saves["saves"] if s["file"] == first)

    ok, r = m.tool("stellaris_load_game", {"campaign": campaign, "file": "no_such_save"})
    record("load_game refuses a missing save", not ok, str(r)[:120])

    ok, r = m.tool("stellaris_load_game", {"campaign": campaign, "file": first})
    record("load_game loads a save and waits for the game", ok and r.get("screen") == "in_game", str(r)[:200])
    if not ok:
        return 1
    record("loaded save has the save's date", r.get("date") == want_date, f"{r.get('date')} vs {want_date}")
    record("start screen handled (closed, or the save has none)", r.get("start_screen") in ("closed", "not_shown"), str(r.get("start_screen")))

    cont = user_dir / "continue_game.json"
    backup = cont.with_suffix(".json.mcp_test_backup")
    if cont.exists():
        shutil.copy2(cont, backup)
    try:
        ok, r = m.tool("stellaris_save_game", {"name": "mcp_test_save", "overwrite": True})
        record("save_game writes the save file", ok and r.get("saved") == f"{campaign}/mcp_test_save.sav", str(r)[:200])
        ok2, r2 = m.tool("stellaris_save_game", {"name": "mcp_test_save"})
        record("save_game refuses an existing name without overwrite", not ok2, str(r2)[:160])
    finally:
        if backup.exists():
            shutil.move(backup, cont)

    ok, scr = m.tool("stellaris_get_status")
    record("no load request left after the load", ok and scr.get("screen") == "in_game" and "quick_start_pending" not in scr, str({k: scr.get(k) for k in ("screen", "quick_start_pending")}))
    ok, r = m.tool("stellaris_exit_to_main_menu")
    record("exit_to_main_menu reaches the main menu", ok and r.get("screen") == "main_menu", str(r)[:120])
    ok, r = m.tool("stellaris_save_game", {"name": "x"})
    record("save_game refuses on the main menu", not ok, str(r)[:120])

    ok, r = m.tool("stellaris_load_game", {"campaign": campaign, "file": "mcp_test_save"})
    record("load_game loads the new save back", ok and r.get("screen") == "in_game" and r.get("date") == want_date, str(r)[:200])
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
