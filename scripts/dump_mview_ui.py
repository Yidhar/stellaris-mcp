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

print(f"ui_win: 0x{ui_win:X}")

def dump_window(win, depth=0):
    if not win or depth > 4: return
    # Window name at +0x168 or +0x20
    name = read_pdx_string(win + 0x168)
    if not name:
        name = read_pdx_string(win + 0x20)
    
    # Children in CContainerWindow
    # +0x878 is children vector
    c_arr = read_ptr(win + 0x878)
    c_cnt = read_u32(win + 0x884)
    
    indent = "  " * depth
    print(f"{indent}- Window: '{name}' (0x{win:X}, children={c_cnt})")
    
    if c_arr and 0 < c_cnt < 100:
        for i in range(c_cnt):
            child = read_ptr(c_arr + i * 8)
            dump_window(child, depth + 1)

dump_window(ui_win)
