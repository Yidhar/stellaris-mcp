import ctypes
import win32process
import win32api
import subprocess
import capstone

out = subprocess.check_output("tasklist /FI \"IMAGENAME eq stellaris.exe\" /FO CSV", shell=True).decode('gbk', errors='ignore')
lines = [l.strip().split('","') for l in out.strip().splitlines() if "stellaris.exe" in l]
pid = int(lines[0][1].strip('"'))
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, size, ctypes.byref(read))
    return buf.raw[:read.value]

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

callers = [0xBC63EE, 0xBC6679, 0x11F7261, 0x12151D7, 0x14CCF9E, 0x14CE9F7, 0x14CF94E]

for c in callers:
    print(f"\n================ Caller at 0x{c:X} ================")
    code = read_bytes(base + c - 0x50, 0x70)
    for i in cs.disasm(code, base + c - 0x50):
        print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")
        if i.address - base == c:
            print("  <--- CALL 0xACDB30")
