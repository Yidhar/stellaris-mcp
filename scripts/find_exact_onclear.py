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

# Search in range 0x1100000 to 0x1300000
d = read_bytes(base + 0x1100000, 0x200000)

# Pattern: mov ecx, 0x18; call (b9 18 00 00 00 e8)
for i in range(len(d) - 20):
    if d[i:i+6] == b'\xb9\x18\x00\x00\x00\xe8':
        rva = 0x1100000 + i
        code = read_bytes(base + rva, 0x60)
        # Disassemble and check if it sets [rax + 8] and [rax + 0xc]
        instructions = list(cs.disasm(code, base + rva))
        inst_text = " ".join([f"{x.mnemonic} {x.op_str}" for x in instructions])
        if "+ 8" in inst_text and "+ 0xc" in inst_text:
            print(f"\nFOUND MATCH AT RVA 0x{rva:X}!")
            for x in instructions[:12]:
                print(f"  0x{x.address - base:X}: {x.mnemonic:8s} {x.op_str}")

