import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def rp(a):
    v = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 8, None)
    return v.value

def ru32(a):
    v = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 4, None)
    return v.value

def read_pdx_string(addr):
    cap = rp(addr + 24)
    sz = rp(addr + 16)
    if sz == 0 or sz > 512: return ""
    buf = (ctypes.c_char * sz)()
    if cap < 16:
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, None)
    else:
        ptr = rp(addr)
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, sz, None)
    return bytes(buf).decode('utf-8', errors='ignore')

idler = rp(base + 0x3113180)
print(f"Idler: 0x{idler:X}")

# Let's inspect idler UI elements
# In idler, where are UI windows?
# Let's search for "136" in the process memory or across CCountry sub-objects
def search_in_obj(addr, name, depth=0):
    if depth > 2 or not addr or addr < 0x10000 or addr > 0x7FFFFFFFFFFF: return
    # read 100 qwords
    buf = (ctypes.c_uint64 * 100)()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, 800, None)
    for i in range(100):
        val = ctypes.c_int64(buf[i]).value
        vf = val / 100000.0
        if -140 < vf < -130:
            print(f"Found match {vf:.2f} at {name} + 0x{i*8:X}")

cmgr = rp(base + 0x3112F50)
carr = rp(cmgr + 0x18)
player = rp(carr + 8)

for off in range(0, 0x3000, 8):
    p = rp(player + off)
    if p > 0x10000 and p < 0x7FFFFFFFFFFF:
        # Check if p is an array of resources
        # read first 30 elements
        buf = (ctypes.c_int64 * 30)()
        kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(p), buf, 240, None)
        for idx in range(30):
            vf = buf[idx] / 100000.0
            if -140 < vf < -130 or 130 < vf < 140:
                print(f"Match {vf:.2f} at player + 0x{off:X} -> arr[{idx}]")

