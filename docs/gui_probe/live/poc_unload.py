import ctypes
import sys
import time

sys.path.insert(0, r"D:\stellaris-perf\bench\scripts")
from benchlib import game_pids, module_loaded  # noqa: E402

k = ctypes.WinDLL("kernel32", use_last_error=True)
k.OpenEventW.restype = ctypes.c_void_p
k.OpenEventW.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p]
k.SetEvent.argtypes = [ctypes.c_void_p]
pids = game_pids()
if not pids:
    print("no game")
    sys.exit(0)
pid = pids[0]
if not module_loaded(pid, "gui_poc.dll"):
    print("gui_poc.dll not loaded")
    sys.exit(0)
ev = k.OpenEventW(0x2, False, f"Local\\gui_poc_unload_{pid}")
k.SetEvent(ev)
for _ in range(60):
    if not module_loaded(pid, "gui_poc.dll"):
        print("gui_poc.dll unloaded itself; game still running:", bool(game_pids()))
        break
    time.sleep(0.2)
else:
    print("still loaded")
