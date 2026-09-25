import os
import sys
import ctypes
from ctypes import wintypes
import time

PROCESS_ALL_ACCESS = 0x1F0FFF
TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010
INFINITE = 0xFFFFFFFF

kernel32 = ctypes.windll.kernel32

class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("th32ModuleID", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("GlblcntUsage", wintypes.DWORD),
        ("ProccntUsage", wintypes.DWORD),
        ("modBaseAddr", ctypes.c_void_p),
        ("modBaseSize", wintypes.DWORD),
        ("hModule", ctypes.c_void_p),
        ("szModule", wintypes.WCHAR * 256),
        ("szExePath", wintypes.WCHAR * 260),
    ]

def find_module(pid, module_name):
    h_snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    if h_snap == -1:
        return None
    me = MODULEENTRY32W()
    me.dwSize = ctypes.sizeof(MODULEENTRY32W)
    found_handle = None
    if kernel32.Module32FirstW(h_snap, ctypes.byref(me)):
        while True:
            if me.szModule.lower() == module_name.lower():
                found_handle = me.hModule
                break
            if not kernel32.Module32NextW(h_snap, ctypes.byref(me)):
                break
    kernel32.CloseHandle(h_snap)
    return found_handle

def eject_dll(pid, module_name="stellaris_bridge.dll"):
    h_k32 = kernel32.GetModuleHandleW("kernel32.dll")
    pfn_free_library = kernel32.GetProcAddress(h_k32, b"FreeLibrary")

    count = 0
    while True:
        h_mod = find_module(pid, module_name)
        if not h_mod:
            if count == 0:
                print(f"[*] Module {module_name} not currently loaded in PID {pid}")
            else:
                print(f"[+] Module {module_name} completely unloaded after {count} FreeLibrary calls.")
            return True

        count += 1
        print(f"[*] Found module {module_name} at 0x{h_mod:X} (attempt {count}). Calling FreeLibrary...")
        h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
        if not h_proc:
            print(f"[-] Failed to open process: {kernel32.GetLastError()}")
            return False

        h_thread = kernel32.CreateRemoteThread(
            h_proc, None, 0, pfn_free_library, ctypes.c_void_p(h_mod), 0, None
        )
        if not h_thread:
            print(f"[-] Failed to create remote thread: {kernel32.GetLastError()}")
            kernel32.CloseHandle(h_proc)
            return False

        kernel32.WaitForSingleObject(h_thread, INFINITE)
        kernel32.CloseHandle(h_thread)
        kernel32.CloseHandle(h_proc)
        time.sleep(0.1)
        if count > 25:
            print(f"[-] Failed to completely unload module after {count} attempts")
            return False

def main():
    import inject
    pid = inject.find_stellaris_pid()
    if not pid:
        print("[-] stellaris.exe not running")
        sys.exit(1)

    print(f"[*] Stellaris PID: {pid}")
    eject_dll(pid, "stellaris_bridge.dll")
    time.sleep(1.0)

    dll_path = os.path.abspath(r"build\stellaris_bridge\Release\stellaris_bridge.dll")
    print(f"[*] Injecting new DLL: {dll_path}")
    ok = inject.inject_dll(pid, dll_path)
    if ok:
        print("[+] Re-injected successfully!")
        time.sleep(1.0)
    else:
        print("[-] Failed to inject.")
        sys.exit(1)

if __name__ == "__main__":
    main()
