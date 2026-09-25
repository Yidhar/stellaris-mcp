import ctypes, time
from ctypes import wintypes
import inject, reload_dll

pid = inject.find_stellaris_pid()
if not pid:
    print("[-] stellaris.exe not found")
    exit(1)
print(f"[*] Found stellaris.exe PID: {pid}")
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF

while True:
    h_mod = reload_dll.find_module(pid, "stellaris_bridge.dll")
    if not h_mod:
        print("[+] Module is fully unloaded!")
        break
    print(f"[*] Calling FreeLibrary on 0x{h_mod:X}...")
    h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    h_k32 = kernel32.GetModuleHandleW("kernel32.dll")
    pfn = kernel32.GetProcAddress(h_k32, b"FreeLibrary")
    h_thread = kernel32.CreateRemoteThread(h_proc, None, 0, pfn, ctypes.c_void_p(h_mod), 0, None)
    kernel32.WaitForSingleObject(h_thread, 0xFFFFFFFF)
    kernel32.CloseHandle(h_thread)
    kernel32.CloseHandle(h_proc)
    time.sleep(0.2)

print("[*] Building newly updated DLL...")
import subprocess
res = subprocess.run([r"F:\vss\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe", "--build", "build", "--config", "Release"], cwd=r"D:\stellarismcp", capture_output=True, text=True, encoding="utf-8", errors="replace")
if res.returncode != 0:
    print("[-] Build failed:")
    print(res.stdout)
    print(res.stderr)
    exit(1)
print("[+] Build succeeded!")

print("[*] Now injecting newly built DLL...")
dll_path = r"D:\stellarismcp\build\stellaris_bridge\Release\stellaris_bridge.dll"
inject.inject_dll(pid, dll_path)
time.sleep(1.0)
print("[+] Done!")
