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

def read_u64(addr):
    buf = ctypes.c_uint64()
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), ctypes.byref(buf), 8, ctypes.byref(read))
    return buf.value

def read_u32(addr):
    buf = ctypes.c_uint32()
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), ctypes.byref(buf), 4, ctypes.byref(read))
    return buf.value

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, size, ctypes.byref(read))
    return buf.raw[:read.value]

# Disassemble 0x9692D0
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
code = read_bytes(base + 0x9692D0, 0x60)
print("=== Disasm 0x9692D0 ===")
for i in cs.disasm(code, base + 0x9692D0):
    print(f"0x{i.address - base:X}: {i.mnemonic} {i.op_str}")
    if i.mnemonic == 'ret':
        break

# Also check what [rip + 0x245aa05] at 0xCB7FFC is:
# next rip = base + 0xCB8003
ptr_addr = base + 0xCB8003 + 0x245aa05
print(f"Global ptr addr: 0x{ptr_addr - base:X}")
val = read_u64(ptr_addr)
print(f"Global ptr val: 0x{val:X}")

# What is [rip + 0x2458d41] at 0xCB7FB0:
# next rip = base + 0xCB7FB7
mgr_addr = base + 0xCB7FB7 + 0x2458d41
print(f"mgr_addr: 0x{mgr_addr - base:X}")
print(f"mgr val: 0x{read_u64(mgr_addr):X}")

# Now let's trace Earth (slot 11) in r10 mgr (0x3112F70)
# r14 = read_u64(0x1D8992FC8A0 + 11*16 + 8) = 0x1D865F40B10
r14 = 0x1D865F40B10
# rcx = r14 + 0x40
rcx = r14 + 0x40
r8d = read_u32(rcx + 0x958)
print(f"r8d at rcx + 0x958: 0x{r8d:X} ({r8d})")
colony_mgr = read_u64(mgr_addr)
cap = read_u32(colony_mgr + 0x20)
arr = read_u64(colony_mgr + 0x18)
idx = r8d & 0xffffff
print(f"colony_mgr cap: {cap}, idx: {idx}")
if idx < cap:
    colony_obj = read_u64(arr + idx * 16 + 8)
    print(f"colony_obj: 0x{colony_obj:X}")
    if colony_obj:
        flag = read_bytes(colony_obj + 0x1280, 1)[0]
        print(f"colony_obj + 0x1280 byte: 0x{flag:X}")

# And what is [r9 - 0x28] where r9 = rcx = r14 + 0x40?
# That is r14 + 0x18
planet_id = read_u32(r14 + 0x18)
print(f"planet_id at r14 + 0x18: {planet_id}")
