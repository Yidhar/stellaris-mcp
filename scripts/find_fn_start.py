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

# Look backwards from 0x121516B for function prologue
# Check bytes between 0x1215000 and 0x121516B
raw = read_bytes(base + 0x1215000, 0x170)
# Look for 0x48, 0x89 or 0x40, 0x53 or 0x48, 0x83, 0xec
# Let's disassemble from 0x1215050
for start in range(0x1215000, 0x1215160, 0x10):
    code = read_bytes(base + start, 0x30)
    for i in cs.disasm(code, base + start):
        if i.mnemonic in ['ret', 'int3']:
            print(f"End of previous fn at 0x{i.address - base:X}")
