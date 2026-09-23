import ctypes
from ctypes import wintypes

import sys
sys.path.append('scripts')
import inject
user32 = ctypes.WinDLL('user32')
pid = inject.find_stellaris_pid()

windows = []
def enum_cb(hwnd, lparam):
    lpdwProcessId = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(lpdwProcessId))
    if lpdwProcessId.value == pid:
        length = user32.GetWindowTextLengthW(hwnd)
        buf = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buf, length + 1)
        vis = user32.IsWindowVisible(hwnd)
        windows.append((hwnd, buf.value, vis))
    return True

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows(WNDENUMPROC(enum_cb), 0)

for h, title, vis in windows:
    print(f"HWND: 0x{h:X}, Title: '{title}', Visible: {vis}")
