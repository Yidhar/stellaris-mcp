"""click.py <client x> <client y> [shot out.png]: move the cursor to a client position of the game window and left-click there."""
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

pid = game_pids()[0]
hwnd = gs.game_window(pid)
user32.keybd_event(0x12, 0, 0, 0)
user32.keybd_event(0x12, 0, 2, 0)
user32.ShowWindow(hwnd, 9)
user32.SetForegroundWindow(hwnd)
time.sleep(0.4)
pt = w.POINT(0, 0)
user32.ClientToScreen(hwnd, ctypes.byref(pt))
x, y = int(sys.argv[1]), int(sys.argv[2])
# step the cursor so the game (and ImGui) see a move before the click
user32.SetCursorPos(pt.x + x - 6, pt.y + y - 6)
time.sleep(0.15)
user32.SetCursorPos(pt.x + x, pt.y + y)
time.sleep(0.25)
user32.mouse_event(0x0002, 0, 0, 0, 0)  # left down
time.sleep(0.08)
user32.mouse_event(0x0004, 0, 0, 0, 0)  # left up
time.sleep(0.5)
print("clicked client", x, y)
