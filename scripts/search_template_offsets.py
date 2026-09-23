import ctypes, struct
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

# Disassemble from base + 0xE14000 to base + 0xE16000
# Search for functions using 0x560 or 0x558
code = read_bytes(base + 0xE14000, 0x3000)

for insn in md.disasm(code, base + 0xE14000):
    if "0x560" in insn.op_str or "0x558" in insn.op_str:
        print(f"0x{insn.address:X} (rel={hex(insn.address-base)}): {insn.mnemonic:8s} {insn.op_str}")
