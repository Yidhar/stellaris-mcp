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

# Find true vtable of CFleet
# For Fleet 3 at 0x270605e6500:
fleet_mgr = r64(base + 0x3113008)
fleet_arr = r64(fleet_mgr + 0x18)
f3 = r64(fleet_arr + 3 * 16 + 8)

for off in range(0, 0x50, 8):
    vt = r64(f3 + off)
    if vt > base and vt < base + 0x3000000:
        fn0 = r64(vt)
        if fn0 > base and fn0 < base + 0x3000000:
            print(f"VT at fleet+{hex(off)}: {hex(vt)} (rel={hex(vt-base)})")
            # print first 5 vfuncs
            for vidx in range(6):
                fn = r64(vt + vidx * 8)
                code = read_bytes(fn, 20)
                dis = [f"{insn.mnemonic} {insn.op_str}" for insn in md.disasm(code, fn)]
                print(f"   [{vidx}] {hex(fn)} (rel={hex(fn-base)}): {'; '.join(dis[:2])}")
