import sys
sys.path.append(r'D:\stellarismcp\scripts')
import ctypes, reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
h_proc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def rp(a):
    v = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 8, None)
    return v.value

def ru32(a):
    v = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 4, None)
    return v.value

def ri64(a):
    v = ctypes.c_int64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 8, None)
    return v.value

def read_pdx_string(addr):
    cap = rp(addr + 24)
    sz = rp(addr + 16)
    if sz == 0 or sz > 512: return ''
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = rp(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

# Earth colony:
cmgr = rp(base + 0x3113140)
arr = rp(cmgr + 0x18)
s0 = rp(arr + 8) # Earth
print(f'Earth colony s0: 0x{s0:X}')

# Let's inspect 0xdd2b70 logic on s0:
# In 0xdd2b70:
# mov rcx, qword ptr [rcx + 0xf78]
# call 0xccc0c0
# mov rsi, rax
# cmp ebx, -1
# je 0xdd2bfb -> mov eax, dword ptr [rsi + 0xc4]
# else cmp ebx, dword ptr [rax] -> if equal: mov eax, dword ptr [rsi + 0xc4]
# else look in array [rbp + 0xb0] for ebx!
f_f78 = rp(s0 + 0xf78)
print(f's0 + 0xf78: 0x{f_f78:X}')

# Let's see what 0xccc0c0 does on f_f78:
# Let's disassemble 0xccc0c0
buf = (ctypes.c_char * 64)()
kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(base + 0xccc0c0), buf, 64, None)
import capstone
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
for i in md.disasm(bytes(buf), base + 0xccc0c0):
    print(f'  0x{i.address - base:X}: {i.mnemonic} {i.op_str}')
    if i.mnemonic == 'ret': break
