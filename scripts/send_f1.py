import ctypes
from ctypes import wintypes
import time

user32 = ctypes.WinDLL('user32')
pid = 73956

stellaris_hwnd = None
def enum_windows_callback(hwnd, lParam):
    global stellaris_hwnd
    lpdwProcessId = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(lpdwProcessId))
    if lpdwProcessId.value == pid:
        if user32.IsWindowVisible(hwnd):
            stellaris_hwnd = hwnd
            return False
    return True

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows(WNDENUMPROC(enum_windows_callback), 0)

print(f"stellaris.exe HWND: 0x{stellaris_hwnd:X}")

WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
VK_F1 = 0x70

print("Sending F1 keydown/keyup...")
user32.PostMessageW(stellaris_hwnd, WM_KEYDOWN, VK_F1, 0)
time.sleep(0.05)
user32.PostMessageW(stellaris_hwnd, WM_KEYUP, VK_F1, 0)
