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

# Let's search for references to CPlanetView vtable
# What is CPlanetView vtable?
# In 0x11DF700, this is a method of CPlanetView!
# Let's find callers of 0x11DF700 or where 0x11DF700 is referenced (in vtable?)
import capstone
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Let's search memory of .rdata for 0x7FF777BD0000 + 0x11DF700
target_addr = base + 0x11DF700
print(f"Searching for pointer to 0x{target_addr:X}...")

# Search in the PE image (.rdata/.data)
# Let's read 0x1000000 bytes around base + 0x2000000
import struct
target_bytes = struct.pack('<Q', target_addr)

chunk_size = 1024 * 1024
for off in range(0x1800000, 0x3000000, chunk_size):
    buf = ctypes.create_string_buffer(chunk_size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(base + off), buf, chunk_size, ctypes.byref(read))
    raw = buf.raw[:read.value]
    pos = 0
    while True:
        pos = raw.find(target_bytes, pos)
        if pos == -1:
            break
        found_rva = off + pos
        print(f"Found reference to 0x11DF700 at RVA 0x{found_rva:X}")
        pos += 8
