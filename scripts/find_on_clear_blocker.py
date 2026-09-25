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

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Look for functions between 0x1200A00 and 0x1201400
# Look for function start after 0x1200A28
d = read_bytes(base + 0x1200A28, 0x800)
# Find int3 (0xCC) padding followed by non-CC
pos = 0
fn_starts = []
while pos < len(d) - 1:
    if d[pos] == 0xCC and d[pos+1] != 0xCC:
        fn_starts.append(0x1200A28 + pos + 1)
    pos += 1

print(f"Functions found after 0x1200A28:")
for fn in fn_starts[:5]:
    print(f"\n--- Function at RVA 0x{fn:X} ---")
    code = read_bytes(base + fn, 0x100)
    for i in cs.disasm(code, base + fn):
        print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")

