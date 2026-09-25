import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def read_ptr(addr):
    val = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 8, None)
    return val.value

def read_u32(addr):
    val = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(val), 4, None)
    return val.value

def read_pdx_string(addr):
    cap = read_ptr(addr + 24)
    sz = read_ptr(addr + 16)
    if sz == 0 or sz > 512: return ""
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = read_ptr(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

res_box = 0x2B4A0D19270
print(f"VT of res_box: 0x{read_ptr(res_box):X}")

# Check children or list inside res_box
# In CGridBox or CListBox or CContainerWindow:
# +0x6F8 (list items) or +0x710 (keys) or +0x878
for off in [0x6F8, 0x700, 0x708, 0x710, 0x718, 0x720, 0x878, 0x880, 0x884, 0x890, 0x89C]:
    val = read_ptr(res_box + off)
    print(f"  +0x{off:X}: 0x{val:X}")

# Let's see if there is an items vector
items = read_ptr(res_box + 0x6F8)
cnt = read_u32(res_box + 0x710)
print(f"Items at 0x6F8: 0x{items:X}, count: {cnt}")

# Let's inspect other fields of res_box from 0 to 0x900
for off in range(0, 0x900, 8):
    p = read_ptr(res_box + off)
    if p > 0x10000000000 and p < 0x7FFFFFFFFFFF:
        # maybe an array
        p0 = read_ptr(p)
        p1 = read_ptr(p + 8)
        # see if it's strings or pointers
        s = read_pdx_string(p + 16)
        if s:
            print(f"  +0x{off:X} -> string '{s}'")
