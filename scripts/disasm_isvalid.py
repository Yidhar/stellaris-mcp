import ctypes
import win32process
import win32api
import capstone
import psutil
import struct

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

cmd_vt = base + 0x23C09F8
vt_raw = read_bytes(cmd_vt, 16 * 8)
for i in range(16):
    fn = struct.unpack('<Q', vt_raw[i*8:(i+1)*8])[0]
    print(f"cmd_vt[{i:2d}]: 0x{fn:X} (RVA 0x{fn - base:X})")

val_fn = struct.unpack('<Q', vt_raw[8*8:9*8])[0]
print(f"\nDisassembling IsValid at 0x{val_fn:X} (RVA 0x{val_fn - base:X}):")
code = read_bytes(val_fn, 0x150)
for i in cs.disasm(code, val_fn):
    print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")

