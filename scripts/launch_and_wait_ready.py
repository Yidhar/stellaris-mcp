import os
import subprocess
import time
import ctypes
from ctypes import wintypes
import inject

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
dll_path = os.path.abspath(r"build\stellaris_bridge\Release\stellaris_bridge.dll")

print("[*] Terminating existing stellaris.exe...")
os.system("taskkill /F /IM stellaris.exe 2>nul")
time.sleep(2)

print(f"[*] Launching Stellaris: {exe_path} -dx11...")
proc = subprocess.Popen([exe_path, "-dx11"], cwd=os.path.dirname(exe_path))
pid = proc.pid
print(f"[+] Started stellaris.exe with PID: {pid}")

print("[*] Waiting for Stellaris window to be created and ready...")
start_time = time.time()
window_ready = False

while time.time() - start_time < 90:
    # Check if process is still alive
    if proc.poll() is not None:
        print(f"[-] Process exited prematurely with code {proc.returncode}")
        break

    # Look for Clausewitz window for this PID
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
        # Check if window is responding
        if not user32.IsHungAppWindow(found_hwnd):
            print(f"[+] Found active Stellaris window (HWND: 0x{found_hwnd:X})")
            # Wait an additional 5 seconds to ensure graphics pipeline is fully steady
            time.sleep(5)
            window_ready = True
            break

    time.sleep(2)

if window_ready:
    print(f"[*] Injecting DLL into PID {pid}...")
    ok = inject.inject_dll(pid, dll_path)
    if ok:
        print("[+] Successfully injected into Stellaris!")
    else:
        print("[-] Failed to inject!")
else:
    print("[-] Stellaris did not become ready in time.")
