import ctypes
import win32process
import win32api
import psutil
import struct
import capstone

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

# Find function start before 0x8004AE
data = read_bytes(base + 0x800000, 0x1000)
# Look backwards from 0x4AE
pos = 0x4AE
while pos > 0:
    # Look for standard prologue: push rbp; sub rsp / push rbx / push r14 etc
    # or int3 (0xCC) padding followed by non-CC
    if data[pos-1] == 0xCC and data[pos] != 0xCC:
        fn_start = 0x800000 + pos
        print(f"Function start for 0x8004AE is 0x{fn_start:X} (RVA 0x{fn_start:X})")
        break
    pos -= 1

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
code = read_bytes(base + fn_start, 0x100)
for i in cs.disasm(code, base + fn_start):
    print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")

# Now search for callers of fn_start
print(f"\nSearching callers of 0x{fn_start:X}:")
for offset in range(0x1000, 0x1800000, 0x100000):
    d = read_bytes(base + offset, 0x100000 + 8)
    for i in range(len(d) - 5):
        if d[i] == 0xE8:
            rel = struct.unpack('<i', d[i+1:i+5])[0]
            target_rva = offset + i + 5 + rel
            if target_rva == fn_start:
                print(f"  Call at RVA 0x{offset + i:X}")

