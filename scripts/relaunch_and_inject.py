import os
import subprocess
import time
import inject

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
dll_path = os.path.abspath(r"build\stellaris_bridge\Release\stellaris_bridge.dll")

print("[*] Terminating existing stellaris.exe...")
os.system("taskkill /F /IM stellaris.exe")
time.sleep(2)

print(f"[*] Launching Stellaris: {exe_path}...")
proc = subprocess.Popen([exe_path], cwd=os.path.dirname(exe_path))
print(f"[+] Started stellaris.exe with PID: {proc.pid}")

print("[*] Waiting for process to initialize (15 seconds)...")
time.sleep(15)

# Wait until main window is ready
pid = inject.find_stellaris_pid()
print(f"[*] Injecting DLL into PID {pid}...")
ok = inject.inject_dll(pid, dll_path)
if ok:
    print("[+] Successfully injected into Stellaris!")
else:
    print("[-] Failed to inject!")
