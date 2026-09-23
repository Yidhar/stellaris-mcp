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

print(f"[*] Launching Stellaris detached: {exe_path}...")
DETACHED_PROCESS = 0x00000008
CREATE_NEW_PROCESS_GROUP = 0x00000200

proc = subprocess.Popen(
    [exe_path, "-dx11"],
    cwd=os.path.dirname(exe_path),
    creationflags=DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
    close_fds=True
)
pid = proc.pid
print(f"[+] Started stellaris.exe with PID: {pid}")

print("[*] Monitoring Stellaris initialization...")
start_time = time.time()
window_ready = False

while time.time() - start_time < 120:
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
            # Check working set memory
            class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
                _fields_ = [
                    ('cb', wintypes.DWORD),
                    ('PageFaultCount', wintypes.DWORD),
                    ('PeakWorkingSetSize', ctypes.c_size_t),
                    ('WorkingSetSize', ctypes.c_size_t),
                    ('QuotaPeakPagedPoolUsage', ctypes.c_size_t),
                    ('QuotaPagedPoolUsage', ctypes.c_size_t),
                    ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t),
                    ('QuotaNonPagedPoolUsage', ctypes.c_size_t),
                    ('PagefileUsage', ctypes.c_size_t),
                    ('PeakPagefileUsage', ctypes.c_size_t),
                ]
            pmc = PROCESS_MEMORY_COUNTERS()
            pmc.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
            h_proc = kernel32.OpenProcess(0x0400 | 0x0010, False, pid)
            ws_mb = 0
            if h_proc:
                if ctypes.windll.psapi.GetProcessMemoryInfo(h_proc, ctypes.byref(pmc), ctypes.sizeof(pmc)):
                    ws_mb = pmc.WorkingSetSize / (1024 * 1024)
                kernel32.CloseHandle(h_proc)

            print(f"[*] Window found (0x{found_hwnd:X}), memory: {ws_mb:.1f} MB (waiting for > 1500 MB)...")
            if ws_mb > 1500:
                print(f"[+] Stellaris main menu loaded successfully (Memory: {ws_mb:.1f} MB)!")
                time.sleep(3)
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
    print("[-] Stellaris did not become ready within timeout.")
