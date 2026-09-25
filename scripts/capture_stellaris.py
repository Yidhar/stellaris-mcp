import ctypes
from ctypes import wintypes
import psutil
from PIL import ImageGrab

user32 = ctypes.windll.user32
pid = 96948
p = psutil.Process(pid)

wins = []
for t in p.threads():
    def cb(h, l):
        title = (ctypes.c_char * 256)()
        user32.GetWindowTextA(h, title, 256)
        t_str = bytes(title).split(b'\x00')[0].decode('utf-8', errors='ignore')
        visible = user32.IsWindowVisible(h)
        print(f"Thread {t.id} HWND: 0x{h:X}, title: '{t_str}', visible: {visible}")
        if visible and t_str:
            wins.append((h, t_str))
        return True
    user32.EnumThreadWindows(t.id, ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(cb), 0)

if wins:
    hwnd = wins[0][0]
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    bbox = (rect.left, rect.top, rect.right, rect.bottom)
    print("Capturing bbox:", bbox)
    im = ImageGrab.grab(bbox)
    im.save("scripts/game_screen.png")
    print("Saved scripts/game_screen.png")
