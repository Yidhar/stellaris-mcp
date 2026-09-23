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

# Disassemble from 0x7FF7789E5C50
code = read_bytes(base + 0xE15C50, 100)
print("=== Disassembly of base + 0xE15C50 ===")
for insn in md.disasm(code, base + 0xE15C50):
    print(f"0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")

# Also from 0x7FF7789E6370
code2 = read_bytes(base + 0xE16370, 100)
print("=== Disassembly of base + 0xE16370 ===")
for insn in md.disasm(code2, base + 0xE16370):
    print(f"0x{insn.address:X}: {insn.mnemonic:8s} {insn.op_str}")
