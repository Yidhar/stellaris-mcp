import win32gui
import win32process
import win32con

def enum_windows_callback(hwnd, extra):
    if win32gui.IsWindowVisible(hwnd):
        title = win32gui.GetWindowText(hwnd)
        _, pid = win32process.GetWindowThreadProcessId(hwnd)
        if pid == 77180:
            print(f"HWND: 0x{hwnd:X}, Title: '{title}', Rect: {win32gui.GetWindowRect(hwnd)}")
            # Bring to foreground if needed
            win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
            win32gui.SetForegroundWindow(hwnd)

win32gui.EnumWindows(enum_windows_callback, None)
