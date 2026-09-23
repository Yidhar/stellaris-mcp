import ctypes, struct
import inject, reload_dll
import capstone

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(read)):
        return buf.raw[:read.value]
    return b""

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Disassemble Execute method of CReinforceFleetCommand (vfunc 4 or 5 of 0x23B90C8)
vt = base + 0x23B90C8
print(f"CReinforceFleetCommand vtable: {hex(vt)}")
for i in range(8):
    fn = r64(vt + i * 8)
    code = read_bytes(fn, 50)
    dis = [f"{insn.mnemonic} {insn.op_str}" for insn in md.disasm(code, fn)]
    print(f"  vfunc[{i}] at {hex(fn)} (rel={hex(fn-base)}): {'; '.join(dis[:4])}")
