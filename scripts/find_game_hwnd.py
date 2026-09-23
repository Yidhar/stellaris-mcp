import ctypes
from ctypes import wintypes
import inject

user32 = ctypes.windll.user32
pid = inject.find_stellaris_pid()
print(f"Stellaris PID: {pid}")

hwnds = []

def enum_cb(hwnd, lparam):
    lp_pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(lp_pid))
    if lp_pid.value == pid:
        length = user32.GetWindowTextLengthW(hwnd)
        buff = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buff, length + 1)
        rect = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
        vis = user32.IsWindowVisible(hwnd)
        hwnds.append((hwnd, buff.value, vis, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top))
    return True

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows(WNDENUMPROC(enum_cb), 0)

for h in hwnds:
    print(f'HWND: 0x{h[0]:X}, Title: "{h[1]}", Visible: {h[2]}, Rect: {h[3]},{h[4]} {h[5]}x{h[6]}')
