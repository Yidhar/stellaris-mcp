import ctypes
import win32process
import win32api
import subprocess
import capstone

out = subprocess.check_output("tasklist /FI \"IMAGENAME eq stellaris.exe\" /FO CSV", shell=True).decode('gbk', errors='ignore')
lines = [l.strip().split('","') for l in out.strip().splitlines() if "stellaris.exe" in l]
pid = int(lines[0][1].strip('"'))
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_str(addr):
    buf = ctypes.create_string_buffer(128)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, 128, ctypes.byref(read))
    s = buf.raw.split(b'\0')[0]
    return s.decode('utf-8', errors='ignore')

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, size, ctypes.byref(read))
    return buf.raw[:read.value]

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
cs.detail = True

# Disassemble 0xBC6300 to 0xBC6500
code = read_bytes(base + 0xBC6300, 0x200)
for i in cs.disasm(code, base + 0xBC6300):
    if 'lea' in i.mnemonic and 'rip' in i.op_str:
        for op in i.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                target = i.address + i.size + op.mem.disp
                s = read_str(target)
                if s and any(c.isprintable() for c in s):
                    print(f"0x{i.address - base:X}: '{s}'")
    print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")
    if i.address - base >= 0xBC6420:
        break
