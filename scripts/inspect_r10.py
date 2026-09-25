import ctypes
import win32process
import win32api
import subprocess

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

# 0x11DF740: mov r10, qword ptr [rip + 0x1f33829] -> next rip = base + 0x11DF747
r10_ptr_addr = base + 0x11DF747 + 0x1f33829
print(f"r10 global ptr addr: 0x{r10_ptr_addr - base:X} (0x{r10_ptr_addr:X})")
r10_val = read_u64(r10_ptr_addr)
print(f"r10 mgr val: 0x{r10_val:X}")

# What is [rip + 0x1f3209e] at 0x11DF753 -> next rip = base + 0x11DF75A
dummy_rdx = base + 0x11DF75A + 0x1f3209e
print(f"dummy_rdx addr: 0x{dummy_rdx - base:X}")

# Look at 0x11DF764:
# mov r8d, dword ptr [r9] (where r9 was rcx + 0xa0)
# eax = r8d & 0xffffff
# r10 is manager!
# r14 = [r10->arr + eax*16 + 8]!
# And then:
# 0x11DF822: lea rcx, [r14 + 0x40]
# 0x11DF826: mov rax, qword ptr [rcx]
# 0x11DF829: call qword ptr [rax + 0x1d8]
# 0x11DF82F: mov r13, rax

print(f"r10 mgr cap: {read_u32(r10_val + 0x20)}")
arr = read_u64(r10_val + 0x18)
print(f"r10 mgr arr: 0x{arr:X}")

# Let's inspect Earth (slot 11) in r10 mgr
earth_slot_11_obj = read_u64(arr + 11 * 16 + 8)
print(f"r10 earth slot 11 obj: 0x{earth_slot_11_obj:X}")

# What is the vtable of r14 (earth_slot_11_obj)?
if earth_slot_11_obj:
    vt_r14 = read_u64(earth_slot_11_obj)
    print(f"r14 vtable: 0x{vt_r14 - base:X}")
    # rcx is r14 + 0x40
    vt_sub = read_u64(earth_slot_11_obj + 0x40)
    print(f"r14 + 0x40 vtable: 0x{vt_sub - base:X}")
    # call [rax + 0x1d8] on rcx = r14 + 0x40:
    fn_ptr = read_u64(vt_sub + 0x1d8)
    print(f"virtual function at [vt_sub + 0x1D8]: 0x{fn_ptr - base:X}")
