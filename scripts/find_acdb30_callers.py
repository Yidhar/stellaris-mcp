import ctypes
import win32process
import win32api
import subprocess
import struct

out = subprocess.check_output("tasklist /FI \"IMAGENAME eq stellaris.exe\" /FO CSV", shell=True).decode('gbk', errors='ignore')
lines = [l.strip().split('","') for l in out.strip().splitlines() if "stellaris.exe" in l]
pid = int(lines[0][1].strip('"'))
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

target_rva = 0xACDB30

chunk_size = 1024 * 1024
for off in range(0x1000, 0x1800000, chunk_size):
    buf = ctypes.create_string_buffer(chunk_size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(base + off), buf, chunk_size, ctypes.byref(read))
    raw = buf.raw[:read.value]
    pos = 0
    while True:
        pos = raw.find(b'\xe8', pos)
        if pos == -1 or pos + 5 > len(raw):
            break
        rel32 = struct.unpack('<i', raw[pos+1:pos+5])[0]
        cur_rva = off + pos
        dest_rva = cur_rva + 5 + rel32
        if dest_rva == target_rva:
            print(f"Found call to 0xACDB30 at RVA 0x{cur_rva:X}")
        pos += 1
