import ctypes
from ctypes import wintypes
user32 = ctypes.windll.user32
def cb(h, l):
    title = (ctypes.c_char * 256)()
    user32.GetWindowTextA(h, title, 256)
    t = bytes(title).split(b'\x00')[0].decode('utf-8', errors='ignore')
    if any(k in t.lower() for k in ['stellaris', 'paradox', '群星']):
        lp_pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(lp_pid))
        print(f'HWND: 0x{h:X}, PID: {lp_pid.value}, title: "{t}"')
    return True
user32.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(cb), 0)
