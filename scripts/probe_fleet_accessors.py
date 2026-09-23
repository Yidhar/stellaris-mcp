import ctypes
import inject, reload_dll
import capstone

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

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

code = read_bytes(base + 0xD64000, 0x2500)

for insn in md.disasm(code, base + 0xD64000):
    # look for small functions or ret
    if insn.mnemonic in ("ret", "retn"):
        addr = insn.address
        # let's look at instructions just before ret if short
        # e.g. within 30 bytes
        prev_code = read_bytes(addr - 20, 20)
        insns = list(md.disasm(prev_code, addr - 20))
        text = " ; ".join([f"{i.mnemonic} {i.op_str}" for i in insns[-4:]])
        if any(w in text for w in ("mov", "cmp", "test")):
            print(f"End of func at 0x{addr:X} (rel={hex(addr-base)}): {text}")
