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

vt = 0x7ff779f088a8
print(f"CFleetTemplate vtable: {hex(vt)} (rel={hex(vt-base)})")
for i in range(15):
    fn = r64(vt + i * 8)
    if fn > base and fn < base + 0x3000000:
        code = read_bytes(fn, 40)
        dis = [f"{insn.mnemonic} {insn.op_str}" for insn in md.disasm(code, fn)]
        print(f"  vfunc[{i}] at {hex(fn)} (rel={hex(fn-base)}): {'; '.join(dis[:4])}")
    else:
        print(f"  vfunc[{i}]: {hex(fn)}")
