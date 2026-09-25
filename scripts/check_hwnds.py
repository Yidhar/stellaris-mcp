import ctypes
from ctypes import wintypes
user32 = ctypes.windll.user32
def cb(h, l):
    lp_pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(h, ctypes.byref(lp_pid))
    if lp_pid.value == 65084:
        title = (ctypes.c_char * 256)()
        user32.GetWindowTextA(h, title, 256)
        vis = user32.IsWindowVisible(h)
        rect = wintypes.RECT()
        user32.GetClientRect(h, ctypes.byref(rect))
        t = bytes(title).split(b'\x00')[0].decode('utf-8', errors='ignore')
        print(f'HWND: 0x{h:X}, vis={vis}, rect=({rect.left},{rect.top},{rect.right},{rect.bottom}), title="{t}"')
    return True
user32.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(cb), 0)
