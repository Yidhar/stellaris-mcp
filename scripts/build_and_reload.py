import os
import sys
import subprocess
import time
import reload_dll
import inject

MSBUILD_PATH = r"F:\vss\MSBuild\Current\Bin\MSBuild.exe"
VCXPROJ_PATH = r"d:\stellarismcp\build\stellaris_bridge\stellaris_bridge.vcxproj"
DLL_PATH = os.path.abspath(r"build\stellaris_bridge\Release\stellaris_bridge.dll")

def main():
    pid = inject.find_stellaris_pid()
    if not pid:
        print("[-] stellaris.exe not running")
        sys.exit(1)

    print(f"[*] Stellaris PID: {pid}")
    print("[*] 1. Ejecting existing DLL...")
    reload_dll.eject_dll(pid, "stellaris_bridge.dll")
    time.sleep(1.0)

    print("[*] 2. Building stellaris_bridge.dll with MSBuild...")
    cmd = [MSBUILD_PATH, VCXPROJ_PATH, "/p:Configuration=Release", "/nologo", "/m"]
    res = subprocess.run(cmd, capture_output=True, encoding='utf-8', errors='ignore')
    if res.returncode != 0:
        print("[-] Build failed:")
        print(res.stdout)
        print(res.stderr)
        sys.exit(1)
    print("[+] Build succeeded!")

    print(f"[*] 3. Re-injecting DLL: {DLL_PATH}")
    ok = inject.inject_dll(pid, DLL_PATH)
    if ok:
        print("[+] Re-injected successfully!")
        time.sleep(1.5)
        # Test ping via named pipe
        import test_pipe
        try:
            with open(test_pipe.PIPE_PATH, "r+b", buffering=0) as pipe:
                resp = test_pipe.send_request(pipe, 1, "ping")
                print("[+] Pipe ping response:", resp)
        except Exception as e:
            print("[-] Warning: Pipe ping failed:", e)
    else:
        print("[-] Failed to inject.")
        sys.exit(1)

if __name__ == "__main__":
    main()
