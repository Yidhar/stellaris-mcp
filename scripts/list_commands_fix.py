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

# Scan from 0 to 0x3500000
chunk_size = 1024 * 1024
cmds = []
for off in range(0, 0x3500000, chunk_size):
    buf = ctypes.create_string_buffer(chunk_size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(base + off), buf, chunk_size, ctypes.byref(read))
    raw = buf.raw[:read.value]
    pos = 0
    while True:
        pos = raw.find(b'Command@@', pos)
        if pos == -1:
            break
        # find start of string before pos
        start = raw.rfind(b'.?AV', max(0, pos - 80), pos)
        if start != -1:
            name = raw[start+4:pos+7].decode('utf-8', errors='ignore')
            cmds.append(name)
        pos += 9

for c in sorted(set(cmds)):
    print(c)
