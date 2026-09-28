"""Regenerate the Stellaris SDK after a game patch.

    python tools/sdk_dumper/dump.py            # full run (live checks if the game is running)
    python tools/sdk_dumper/dump.py --no-live  # static only
    python tools/sdk_dumper/dump.py --skip-linux  # reuse out/linux_index.json (no source/ needed)

Stages: linux_index -> win_extract -> validate -> emit_sdk -> globals -> functions -> [live_verify] -> emit_sdk
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def run(script, check=True):
    print(f"\n=== {script} ===", flush=True)
    r = subprocess.run([sys.executable, str(HERE / script)])
    if check and r.returncode != 0:
        raise SystemExit(f"{script} failed ({r.returncode})")
    return r.returncode


def game_running():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq stellaris.exe", "/NH"], capture_output=True, text=True).stdout
    return "stellaris.exe" in out.lower()


def main():
    args = set(sys.argv[1:])
    if "--skip-linux" not in args:
        run("linux_index.py")
    run("win_extract.py")
    rc = run("validate.py", check=False)
    run("emit_sdk.py")
    run("globals.py")
    run("functions.py")
    if "--no-live" not in args and game_running():
        run("live_verify.py", check=False)
    elif "--no-live" not in args:
        print("\n(stellaris.exe not running: skipped live verification; database picks are static only)")
        stale = HERE / "out" / "globals_verified.json"
        if stale.exists():
            stale.unlink()
    run("emit_sdk.py")
    if rc:
        raise SystemExit("validate.py reported mismatches against hand-verified layouts -- review before use")


if __name__ == "__main__":
    main()
