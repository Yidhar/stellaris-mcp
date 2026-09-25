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

idler = read_ptr(base + 0x3113180)
mview = read_ptr(idler + 0xDC0)
ui_win = read_ptr(mview + 0x78)

def dump_named_children(win, name="root", depth=0):
    vec_names = read_ptr(win + 0x890)
    names_cnt = read_u32(win + 0x89C)
    c_arr = read_ptr(win + 0x878)
    c_cnt = read_u32(win + 0x884)
    indent = "  " * depth
    print(f"{indent}[{name}] (0x{win:X}) - {names_cnt} names, {c_cnt} children")
    if vec_names and names_cnt > 0:
        for i in range(min(names_cnt, c_cnt)):
            c_name = read_pdx_string(vec_names + i * 48 + 16)
            child = read_ptr(c_arr + i * 8)
            print(f"{indent}  - Child {i}: '{c_name}' -> 0x{child:X}")
            if depth < 3:
                dump_named_children(child, c_name, depth + 1)

dump_named_children(ui_win)
