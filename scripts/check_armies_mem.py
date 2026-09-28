import ctypes, win32api, win32process, subprocess

out = subprocess.check_output('tasklist /FI "IMAGENAME eq stellaris.exe" /FO CSV', shell=True).decode('gbk', errors='ignore')
lines = [l.strip().split('","') for l in out.strip().splitlines() if "stellaris.exe" in l]
pid = int(lines[0][1].strip('"'))
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]
kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)

def read_u64(addr):
    buf = ctypes.c_uint64()
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), ctypes.byref(buf), 8, ctypes.byref(read)):
        return buf.value
    return 0

def read_u32(addr):
    buf = ctypes.c_uint32()
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), ctypes.byref(buf), 4, ctypes.byref(read)):
        return buf.value
    return 0

print(f"PID: {pid}, Base: {hex(base)}")
for name, rva in [
    ("CToggleDeployArmiesInOrbitCommand", 0x2391038),
    ("CToggleIncludeInArmyBuilderCommand", 0x2390F80),
    ("CMoveArmyToOrbitCommand", 0x23CB648),
    ("CDisbandArmyCommand", 0x258E508),
    ("CAddBuildableToQueueCommand", 0x2393358),
]:
    v = read_u64(base + rva)
    print(f"{name} (RVA {hex(rva)}): 0x{v:X}")
