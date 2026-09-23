import ctypes, struct
import inject, reload_dll
import sys
sys.stdout.reconfigure(encoding='utf-8')

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(read)):
        return buf.raw[:read.value]
    return b""

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read)):
        return struct.unpack('<I', buf.raw)[0]
    return 0

# Search for strings related to ship design commands
# Read .rdata section
# Let's search chunks of .rdata (typically base + 0x2200000 to base + 0x2A00000)
targets = [
    b"CCreateDesignCommand",
    b"CChangeDesignCommand",
    b"CDeleteDesignCommand",
    b"CUpgradeFleetCommand",
    b"create_design",
    b"change_design",
    b"delete_design",
    b"upgrade_fleet"
]

print("Scanning for command strings...")
chunk_size = 0x100000
for page_start in range(base + 0x2000000, base + 0x2A00000, chunk_size):
    chunk = read_bytes(page_start, chunk_size)
    if not chunk: continue
    for t in targets:
        pos = 0
        while True:
            idx = chunk.find(t, pos)
            if idx == -1: break
            addr = page_start + idx
            print(f"Found '{t.decode()}' at {hex(addr)} (rel={hex(addr-base)})")
            pos = idx + len(t)
