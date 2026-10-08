"""Live driver for the ImGui showcase (needs the perf repo's bench scripts: D:\\stellaris-perf\\bench\\scripts).

    python showcase_test.py install-mod       copy the test mod into the user's mod folder and enable it in dlc_load.json (backup kept)
    python showcase_test.py uninstall-mod     put dlc_load.json back and remove the test mod
    python showcase_test.py load              restart the game on the test save (mods are read at start)
    python showcase_test.py inject <dll>      copy the DLL into the run folder and inject it with `stl inject`
    python showcase_test.py cmd "tab 3" ...   write lines into gui_showcase.cmd (the DLL picks them up within ~20 frames)
    python showcase_test.py log [n]           tail of gui_showcase.log
    python showcase_test.py shot out.png      screenshot of the game window's client area
    python showcase_test.py unload            ask the DLL to unload itself
"""
import ctypes
import ctypes.wintypes as w
import json
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, r"D:\stellaris-perf\bench\scripts")
import game_session as gs  # noqa: E402
from benchlib import game_pids, module_loaded  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
MOD_SRC = os.path.join(HERE, "..", "mod", "zz_gui_showcase")
DOCS = gs.DOCS
MOD_DIR = os.path.join(DOCS, "mod")
DLC = os.path.join(DOCS, "dlc_load.json")
DLC_BACKUP = DLC + ".gui_backup"
RUN = os.environ.get("SHOWCASE_RUN", os.path.join(os.environ.get("CLAUDE_JOB_DIR", HERE), "tmp", "showcase_run"))
STL = r"D:\stellaris-Launcher\target\release\stl.exe"

user32 = ctypes.WinDLL("user32", use_last_error=True)
try:
    ctypes.WinDLL("shcore").SetProcessDpiAwareness(2)
except OSError:
    user32.SetProcessDPIAware()


def install_mod():
    dst = os.path.join(MOD_DIR, "zz_gui_showcase")
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(MOD_SRC, dst)
    with open(os.path.join(MOD_DIR, "zz_gui_showcase.mod"), "w", encoding="utf-8") as f:
        f.write('name="zz gui showcase"\nversion="1"\ntags={\n\t"Utilities"\n}\nsupported_version="v4.5.*"\n')
        f.write('path="%s"\n' % dst.replace("\\", "/"))
    if not os.path.exists(DLC_BACKUP):
        shutil.copyfile(DLC, DLC_BACKUP)
    cfg = json.load(open(DLC, encoding="utf-8"))
    if "mod/zz_gui_showcase.mod" not in cfg["enabled_mods"]:
        cfg["enabled_mods"].append("mod/zz_gui_showcase.mod")
    json.dump(cfg, open(DLC, "w", encoding="utf-8"), separators=(",", ":"))
    print("mod installed and enabled:", cfg["enabled_mods"])


def uninstall_mod():
    if os.path.exists(DLC_BACKUP):
        shutil.copyfile(DLC_BACKUP, DLC)
        os.remove(DLC_BACKUP)
        print("dlc_load.json restored")
    shutil.rmtree(os.path.join(MOD_DIR, "zz_gui_showcase"), ignore_errors=True)
    p = os.path.join(MOD_DIR, "zz_gui_showcase.mod")
    if os.path.exists(p):
        os.remove(p)
    print("mod removed")


def one_game():
    pids = game_pids()
    if len(pids) != 1:
        raise SystemExit(f"expected exactly one stellaris.exe, found {pids}")
    return pids[0]


def inject(dll):
    pid = one_game()
    os.makedirs(RUN, exist_ok=True)
    dst = os.path.join(RUN, "gui_showcase.dll")
    if module_loaded(pid, "gui_showcase.dll"):
        raise SystemExit("gui_showcase.dll is already loaded; unload it first")
    shutil.copyfile(dll, dst)
    r = subprocess.run([STL, "inject", dst, "--pid", str(pid)], capture_output=True, text=True)
    print(r.stdout.strip(), r.stderr.strip())
    time.sleep(1.5)
    print("loaded:", module_loaded(pid, "gui_showcase.dll"))


def unload():
    pid = one_game()
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    k.OpenEventW.restype = ctypes.c_void_p
    k.OpenEventW.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p]
    k.SetEvent.argtypes = [ctypes.c_void_p]
    ev = k.OpenEventW(0x2, False, f"Local\\gui_showcase_unload_{pid}")
    if not ev:
        raise SystemExit("unload event not found (DLL not loaded?)")
    k.SetEvent(ev)
    for _ in range(80):
        if not module_loaded(pid, "gui_showcase.dll"):
            print("unloaded; game still running:", bool(game_pids()))
            return
        time.sleep(0.2)
    print("still loaded")


def cmd(lines):
    os.makedirs(RUN, exist_ok=True)
    p = os.path.join(RUN, "gui_showcase.cmd")
    tmp = p + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(tmp, p)
    for _ in range(60):  # the DLL deletes the file once it has run it
        if not os.path.exists(p):
            return
        time.sleep(0.1)
    print("warning: the command file was not picked up (DLL not drawing frames? ImGui not running?)")


def log(n):
    p = os.path.join(RUN, "gui_showcase.log")
    if os.path.exists(p):
        for line in open(p, encoding="utf-8", errors="replace").read().splitlines()[-n:]:
            print(line)


def front(pid):
    hwnd = gs.game_window(pid)
    user32.keybd_event(0x12, 0, 0, 0)
    user32.keybd_event(0x12, 0, 0x2, 0)
    user32.ShowWindow(hwnd, 9)
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.5)
    return hwnd


def shot(out):
    from PIL import ImageGrab
    pid = one_game()
    hwnd = front(pid)
    pt = w.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    c = w.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(c))
    vx, vy = user32.GetSystemMetrics(76), user32.GetSystemMetrics(77)
    im = ImageGrab.grab(all_screens=True).crop((pt.x - vx, pt.y - vy, pt.x - vx + c.right, pt.y - vy + c.bottom))
    im.save(out)
    print(f"saved {out} ({c.right}x{c.bottom})")


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
    elif a[0] == "install-mod":
        install_mod()
    elif a[0] == "uninstall-mod":
        uninstall_mod()
    elif a[0] == "load":
        gs.load("fmbase", "11_638438808", 420)
    elif a[0] == "inject":
        inject(a[1])
    elif a[0] == "cmd":
        cmd(a[1:])
    elif a[0] == "bench":  # bench DLL commands, e.g. `bench "pause 0" "speed 3"` (used before the ImGui is up, to get ticks going)
        from benchlib import Bench
        b = Bench(tries=50)
        for line in a[1:]:
            print(line, "->", b.cmd(line))
        b.close()
    elif a[0] == "log":
        log(int(a[1]) if len(a) > 1 else 40)
    elif a[0] == "shot":
        shot(a[1])
    elif a[0] == "unload":
        unload()
    else:
        print(__doc__)


main()
