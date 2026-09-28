"""Launch Stellaris directly (no Steam / Paradox launcher), exactly one instance.

    python scripts/launch_stellaris.py            # launch unless the game is already running
    python scripts/launch_stellaris.py --restart  # close every running instance first

Launching while an instance is running leaves the old process alive without a window, and
both instances then host a bridge on the same named pipe. So this script refuses to start a
second instance and waits for the new one to own the game window.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import inject  # noqa: E402

GAME_EXE = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
GAME_DIR = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris"


def close_running(timeout=30):
    pids = inject.list_stellaris_pids()
    if not pids:
        return True
    print(f"[*] Closing running stellaris.exe {pids}...")
    subprocess.run(["taskkill", "/F", "/IM", "stellaris.exe"], capture_output=True)
    end = time.time() + timeout
    while time.time() < end:
        if not inject.list_stellaris_pids():
            return True
        time.sleep(0.5)
    print(f"[-] stellaris.exe still running: {inject.list_stellaris_pids()}")
    return False


def launch_stellaris(restart=False, wait=120):
    """Returns the PID of the single game instance, or None."""
    if not os.path.exists(GAME_EXE):
        print(f"[-] Stellaris executable not found at: {GAME_EXE}")
        return None

    running = inject.list_stellaris_pids()
    if running and not restart:
        pid = inject.find_stellaris_pid()
        print(f"[!] Stellaris is already running {running}; not starting another instance "
              f"(use --restart to relaunch). Game PID: {pid}")
        return pid
    if running and not close_running():
        return None

    print(f"[*] Starting Stellaris directly: {GAME_EXE} -dx11")
    bat_path = os.path.join(os.environ["TEMP"], "launch_stellaris.bat")
    with open(bat_path, "w") as f:
        f.write(f'@cd /d "{GAME_DIR}"\n@start "" "{GAME_EXE}" -dx11\n')
    subprocess.Popen(["explorer.exe", bat_path])

    end = time.time() + wait
    while time.time() < end:
        pids = inject.list_stellaris_pids()
        windowed = [p for p in pids if p in inject.pids_with_game_window()]
        if windowed:
            if len(pids) > 1:
                print(f"[!] Unexpected extra stellaris.exe processes: {pids}")
            print(f"[+] Stellaris window is up (PID {windowed[0]})")
            return windowed[0]
        time.sleep(1)
    print("[-] Stellaris window did not appear in time")
    return None


if __name__ == "__main__":
    ok = launch_stellaris(restart="--restart" in sys.argv)
    sys.exit(0 if ok else 1)
