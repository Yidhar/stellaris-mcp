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

# Search for classes whose name starts with C and ends with Command
chunk_size = 1024 * 1024
cmds = []
for off in range(0x1800000, 0x3000000, chunk_size):
    buf = ctypes.create_string_buffer(chunk_size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(base + off), buf, chunk_size, ctypes.byref(read))
    raw = buf.raw[:read.value]
    pos = 0
    while True:
        pos = raw.find(b'.?AVC', pos)
        if pos == -1:
            break
        end = raw.find(b'@@', pos)
        if end != -1 and end - pos < 100:
            name = raw[pos+4:end].decode('utf-8', errors='ignore')
            if name.endswith('Command'):
                cmds.append(name)
            pos = end + 2
        else:
            pos += 4

for c in sorted(set(cmds)):
    print(c)
