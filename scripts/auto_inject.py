import os
import sys
import time
import ctypes
from ctypes import wintypes
from inject import find_stellaris_pid, inject_dll

user32 = ctypes.windll.user32

def wait_for_window(pid, timeout=90):
    print(f"[*] Waiting for stellaris.exe (PID {pid}) window to be ready...")
    start_time = time.time()
    while time.time() - start_time < timeout:
        found_hwnds = []
        def enum_cb(hwnd, lparam):
            lp_pid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(lp_pid))
            if lp_pid.value == pid and user32.IsWindowVisible(hwnd):
                rect = wintypes.RECT()
                user32.GetClientRect(hwnd, ctypes.byref(rect))
                w = rect.right - rect.left
                h = rect.bottom - rect.top
                if w > 400 and h > 300:
                    found_hwnds.append(hwnd)
                    return False
            return True

        WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        user32.EnumWindows(WNDENUMPROC(enum_cb), 0)

        if found_hwnds:
            found_hwnd = found_hwnds[0]
            if not user32.IsHungAppWindow(found_hwnd):
                print(f"[+] Found active Stellaris window (HWND: 0x{found_hwnd:X}). Waiting 8s for graphics pipeline...")
                time.sleep(8)
                return True
        time.sleep(2)
    return False

def main():
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_dll = os.path.join(root_dir, "build", "stellaris_bridge", "Release", "stellaris_bridge.dll")
    
    print("[*] Waiting for stellaris.exe process to appear...")
    while True:
        pid = find_stellaris_pid()
        if pid:
            print(f"[+] Found stellaris.exe with PID: {pid}")
            if wait_for_window(pid):
                if inject_dll(pid, default_dll):
                    print("[+] Injection complete!")
                    break
                else:
                    print("[-] Injection failed, retrying in 2 seconds...")
            else:
                print("[-] Window did not become ready.")
                break
        time.sleep(1)

if __name__ == "__main__":
    main()
