"""Stellaris DLL Injector using Win32 CreateRemoteThread.
"""

import os
import sys
import ctypes
from ctypes import wintypes
import time

# Windows Constants
PROCESS_ALL_ACCESS = 0x1F0FFF
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04
INFINITE = 0xFFFFFFFF
TH32CS_SNAPPROCESS = 0x00000002

class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.c_size_t),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", wintypes.LONG),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", wintypes.WCHAR * wintypes.MAX_PATH),
    ]

kernel32 = ctypes.windll.kernel32

# Configure 64-bit argument and return types for Win32 API functions
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE

kernel32.VirtualAllocEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
kernel32.VirtualAllocEx.restype = wintypes.LPVOID

kernel32.WriteProcessMemory.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.LPCVOID, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.WriteProcessMemory.restype = wintypes.BOOL

kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
kernel32.GetModuleHandleW.restype = wintypes.HMODULE

kernel32.GetProcAddress.argtypes = [wintypes.HMODULE, wintypes.LPCSTR]
kernel32.GetProcAddress.restype = wintypes.LPVOID

kernel32.CreateRemoteThread.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.LPVOID, wintypes.LPVOID, wintypes.DWORD, wintypes.LPDWORD]
kernel32.CreateRemoteThread.restype = wintypes.HANDLE

kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel32.WaitForSingleObject.restype = wintypes.DWORD

kernel32.GetExitCodeThread.argtypes = [wintypes.HANDLE, wintypes.LPDWORD]
kernel32.GetExitCodeThread.restype = wintypes.BOOL

kernel32.VirtualFreeEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD]
kernel32.VirtualFreeEx.restype = wintypes.BOOL

kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL


def find_stellaris_pid() -> int | None:
    h_snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if h_snapshot == -1:
        return None

    pe32 = PROCESSENTRY32W()
    pe32.dwSize = ctypes.sizeof(PROCESSENTRY32W)

    pids = []
    if kernel32.Process32FirstW(h_snapshot, ctypes.byref(pe32)):
        while True:
            if pe32.szExeFile.lower() == "stellaris.exe":
                pids.append(pe32.th32ProcessID)
            if not kernel32.Process32NextW(h_snapshot, ctypes.byref(pe32)):
                break

    kernel32.CloseHandle(h_snapshot)
    return pids[-1] if pids else None


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("th32ModuleID", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("GlblcntUsage", wintypes.DWORD),
        ("ProccntUsage", wintypes.DWORD),
        ("modBaseAddr", ctypes.c_void_p),
        ("modBaseSize", wintypes.DWORD),
        ("hModule", wintypes.HMODULE),
        ("szModule", wintypes.WCHAR * 256),
        ("szExePath", wintypes.WCHAR * 260),
    ]

def inject_dll(pid: int, dll_path: str) -> bool:
    abs_dll_path = os.path.abspath(dll_path)
    if not os.path.exists(abs_dll_path):
        print(f"[-] DLL not found: {abs_dll_path}")
        return False

    print(f"[*] Target PID: {pid}")
    print(f"[*] DLL to inject: {abs_dll_path}")

    h_process = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    if not h_process:
        print(f"[-] OpenProcess failed. Error code: {kernel32.GetLastError()}")
        return False

    try:
        # Encode DLL path as UTF-16LE with null terminator
        dll_bytes = abs_dll_path.encode("utf-16le") + b"\x00\x00"

        # Allocate memory in target process (64-bit safe)
        p_remote_mem = kernel32.VirtualAllocEx(
            h_process, None, len(dll_bytes), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE
        )
        if not p_remote_mem:
            print(f"[-] VirtualAllocEx failed. Error code: {kernel32.GetLastError()}")
            return False

        print(f"[*] Allocated remote memory at: {hex(p_remote_mem)}")

        # Write DLL path to target process
        bytes_written = ctypes.c_size_t(0)
        ok = kernel32.WriteProcessMemory(
            h_process, p_remote_mem, dll_bytes, len(dll_bytes), ctypes.byref(bytes_written)
        )
        if not ok:
            print(f"[-] WriteProcessMemory failed. Error code: {kernel32.GetLastError()}")
            return False

        # Get address of LoadLibraryW
        h_kernel32 = kernel32.GetModuleHandleW("kernel32.dll")
        pfn_load_library = kernel32.GetProcAddress(h_kernel32, b"LoadLibraryW")
        if not pfn_load_library:
            print("[-] Could not find LoadLibraryW.")
            return False

        # Create remote thread
        thread_id = wintypes.DWORD(0)
        h_thread = kernel32.CreateRemoteThread(
            h_process, None, 0, pfn_load_library, p_remote_mem, 0, ctypes.byref(thread_id)
        )
        if not h_thread:
            print(f"[-] CreateRemoteThread failed. Error code: {kernel32.GetLastError()}")
            return False

        print(f"[+] Remote thread created (ThreadID: {thread_id.value}). Waiting for completion...")
        kernel32.WaitForSingleObject(h_thread, INFINITE)

        exit_code = wintypes.DWORD(0)
        kernel32.GetExitCodeThread(h_thread, ctypes.byref(exit_code))
        kernel32.CloseHandle(h_thread)

        # Free remote memory
        kernel32.VirtualFreeEx(h_process, p_remote_mem, 0, MEM_RELEASE)

        if exit_code.value == 0:
            print("[-] LoadLibraryW returned NULL in remote process. Injection failed.")
            return False

        print(f"[+] Injected successfully! Injected module handle: {hex(exit_code.value)}")
        return True

    finally:
        kernel32.CloseHandle(h_process)


def main():
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_dll = os.path.join(root_dir, "build", "stellaris_bridge", "Release", "stellaris_bridge.dll")

    dll_path = sys.argv[1] if len(sys.argv) > 1 else default_dll

    pid = find_stellaris_pid()
    if not pid:
        print("[-] stellaris.exe is not currently running. Please launch Stellaris first.")
        sys.exit(1)

    success = inject_dll(pid, dll_path)
    if success:
        print("[+] Waiting for IPC Named Pipe to initialize...")
        time.sleep(1)
        pipe_path = r"\\.\pipe\stellaris_mcp_bridge"
        if os.path.exists(pipe_path):
            print(f"[+] Pipe {pipe_path} is ACTIVE and ready for commands!")
        else:
            print(f"[*] Note: Pipe {pipe_path} may take an extra moment to appear after frame Present.")
    else:
        sys.exit(1)


if __name__ == "__main__":
    main()
