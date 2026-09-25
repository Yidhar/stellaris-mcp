import os
import sys
import time
import subprocess

GAME_EXE = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
GAME_DIR = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris"

def launch_stellaris():
    if not os.path.exists(GAME_EXE):
        print(f"[-] Stellaris executable not found at: {GAME_EXE}")
        return False
    print(f"[*] Starting Stellaris directly: {GAME_EXE}")
    bat_path = os.path.join(os.environ["TEMP"], "launch_stellaris.bat")
    with open(bat_path, "w") as f:
        f.write(f'@cd /d "{GAME_DIR}"\n@start "" "{GAME_EXE}" -dx11\n')
    
    subprocess.Popen(["explorer.exe", bat_path])
    time.sleep(3)
    return True

if __name__ == "__main__":
    launch_stellaris()


