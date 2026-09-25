import ctypes
from ctypes import wintypes
import psutil
user32 = ctypes.windll.user32
p = psutil.Process(65084)
for t in p.threads():
    def cb(h, l):
        title = (ctypes.c_char * 256)()
        user32.GetWindowTextA(h, title, 256)
        t_str = bytes(title).split(b'\x00')[0].decode('utf-8', errors='ignore')
        print(f'Thread {t.id} HWND: 0x{h:X}, title: "{t_str}"')
        return True
    user32.EnumThreadWindows(t.id, ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(cb), 0)
