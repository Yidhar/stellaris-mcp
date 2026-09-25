import ctypes
import win32process
import win32api
import capstone
import psutil
import struct

# Find stellaris.exe PID
pid = None
for p in psutil.process_iter(['pid', 'name']):
    if p.info['name'] and p.info['name'].lower() == 'stellaris.exe':
        pid = p.info['pid']
        break

h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, size, ctypes.byref(read))
    return buf.raw[:read.value]

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# 1. Search references to 0x2390F30 (CBuildableClearDepositBlocker vtable)
vt_addr = base + 0x2390F30
vt_bytes = struct.pack('<Q', vt_addr)

print(f"Searching references to vtable 0x{vt_addr:X}...")
# Scan text section (0x1000 to 0x2000000)
chunk_size = 0x100000
for offset in range(0x1000, 0x2000000, chunk_size):
    data = read_bytes(base + offset, chunk_size + 8)
    # Check absolute 64-bit pointer
    pos = 0
    while True:
        idx = data.find(vt_bytes, pos)
        if idx == -1:
            break
        rva = offset + idx
        print(f"Found absolute ref at RVA 0x{rva:X}")
        pos = idx + 1

    # Check RIP-relative: lea rcx/rax, [rip + disp32] where rip + disp32 + 7 == vt_addr
    # lea rax/rcx/rdx/r8, [rip + disp32]: 48 8d 05/0d/15/.. disp32
    # In x64: 48 8d ?? xx xx xx xx
    for i in range(len(data) - 7):
        if data[i] in (0x48, 0x4c) and data[i+1] == 0x8d:
            disp = struct.unpack('<i', data[i+3:i+7])[0]
            target = base + offset + i + 7 + disp
            if target == vt_addr:
                rva = offset + i
                print(f"Found RIP-relative ref (lea) at RVA 0x{rva:X}")

