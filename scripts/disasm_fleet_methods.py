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

# Disassemble from base + 0xD64580
code = read_bytes(base + 0xD64580, 200)
print("=== Disassembly of CFleet methods (base + 0xD64580) ===")
for insn in md.disasm(code, base + 0xD64580):
    print(f"0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")
