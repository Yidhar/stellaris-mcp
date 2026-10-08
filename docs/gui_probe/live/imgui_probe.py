"""Live probe: does the engine's own Dear ImGui work in the release game?

    python imgui_probe.py load                 restart the game on the test save (perf repo's game_session; pauses at speed 4)
    python imgui_probe.py cmd "imgui on" ...   type console commands (grave key opens the console), one after the other
    python imgui_probe.py shot out.png         screenshot of the game window's client area (game must be in front)
"""
import ctypes
import ctypes.wintypes as w
import sys
import time

sys.path.insert(0, r"D:\stellaris-perf\bench\scripts")
import game_session as gs  # noqa: E402
from benchlib import game_pids  # noqa: E402

user32 = ctypes.WinDLL("user32", use_last_error=True)
try:
    ctypes.WinDLL("shcore").SetProcessDpiAwareness(2)
except OSError:
    user32.SetProcessDPIAware()

VK = {" ": 0x20, "\n": 0x0D, "_": 0xBD, "`": 0xC0}
KEYUP, SCAN = 0x2, 0x8


def vk_of(ch):
    if ch in VK:
        return VK[ch], ch == "_"
    if ch.isalnum():
        return ord(ch.upper()), False
    raise ValueError(ch)


def send(scan, up):
    inp = gs.INPUT(type=1, ki=gs.KEYBDINPUT(wVk=0, wScan=scan, dwFlags=SCAN | (KEYUP if up else 0), time=0, dwExtraInfo=None))
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(gs.INPUT))


def tap(vk, shift=False):
    scan = user32.MapVirtualKeyW(vk, 0)
    if shift:
        send(user32.MapVirtualKeyW(0x10, 0), False)
    send(scan, False)
    time.sleep(0.03)
    send(scan, True)
    if shift:
        send(user32.MapVirtualKeyW(0x10, 0), True)
    time.sleep(0.03)


def front(pid):
    hwnd = gs.game_window(pid)
    user32.keybd_event(0x12, 0, 0, 0)
    user32.keybd_event(0x12, 0, KEYUP, 0)
    user32.ShowWindow(hwnd, 9)
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.4)
    return hwnd


def console(pid, line):
    front(pid)
    tap(0xC0)  # grave: opens/closes the console
    time.sleep(0.4)
    for ch in line:
        vk, shift = vk_of(ch)
        tap(vk, shift)
    tap(0x0D)
    time.sleep(0.4)
    tap(0xC0)  # close it again so the game shows what the command did
    time.sleep(0.6)


def shot(pid, out):
    from PIL import ImageGrab
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
    if a[0] == "load":
        gs.load("fmbase", "11_638438808", 420)
        return
    pids = game_pids()
    if len(pids) != 1:
        raise SystemExit(f"expected one stellaris.exe, found {pids}")
    pid = pids[0]
    if a[0] == "cmd":
        for line in a[1:]:
            print("console:", line)
            console(pid, line)
    elif a[0] == "shot":
        shot(pid, a[1])


main()
