import ctypes
from ctypes import wintypes
import subprocess

out = subprocess.check_output('tasklist /FI "IMAGENAME eq stellaris.exe" /FO CSV /NH', shell=True).decode()
pid = int(out.split(',')[1].strip(' "\r\n'))

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
h = ctypes.windll.kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)

hmods = (wintypes.HMODULE * 1024)()
cb = wintypes.DWORD()
ok = ctypes.windll.psapi.EnumProcessModulesEx(h, hmods, ctypes.sizeof(hmods), ctypes.byref(cb), 0x03)
if ok:
    base = hmods[0]
    print(f"stellaris.exe base address in PID {pid}: 0x{base:X}")
else:
    print(f"EnumProcessModulesEx failed: {ctypes.GetLastError()}")
