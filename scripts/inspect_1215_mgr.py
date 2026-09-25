import ctypes
import win32process
import win32api
import subprocess

out = subprocess.check_output("tasklist /FI \"IMAGENAME eq stellaris.exe\" /FO CSV", shell=True).decode('gbk', errors='ignore')
lines = [l.strip().split('","') for l in out.strip().splitlines() if "stellaris.exe" in l]
pid = int(lines[0][1].strip('"'))
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_u64(addr):
    buf = ctypes.c_uint64()
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), ctypes.byref(buf), 8, ctypes.byref(read))
    return buf.value

# 0x121516B: mov rdx, qword ptr [rip + 0x1efddfe] -> next rip = base + 0x1215172
mgr_1215 = base + 0x1215172 + 0x1efddfe
print(f"mgr_1215 addr: 0x{mgr_1215 - base:X}")
print(f"mgr_1215 val: 0x{read_u64(mgr_1215):X}")

# 0x12151A0: mov rcx, qword ptr [rip + 0x1efc651] -> next rip = base + 0x12151A7
dummy_1215 = base + 0x12151A7 + 0x1efc651
print(f"dummy_1215 addr: 0x{dummy_1215 - base:X}")
print(f"dummy_1215 val: 0x{read_u64(dummy_1215):X}")
