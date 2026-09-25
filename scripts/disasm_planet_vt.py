import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject, capstone

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def rp(a):
    v = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 8, None)
    return v.value

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

vt = 0x7FF779F6FB40
print(f"Disassembling methods of CPlanet vtable 0x{vt:X}:")

for idx in range(30):
    fn = rp(vt + idx * 8)
    code_buf = (ctypes.c_char * 32)()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(fn), code_buf, 32, None)
    dis = list(md.disasm(bytes(code_buf), fn))
    insn_str = "; ".join([f"{i.mnemonic} {i.op_str}" for i in dis[:3]])
    print(f"  slot[{idx}] (0x{fn - base:X}): {insn_str}")
