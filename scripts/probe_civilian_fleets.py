import ctypes, struct
import inject, reload_dll
import sys
sys.stdout.reconfigure(encoding='utf-8')

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
kernel32 = ctypes.windll.kernel32
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def r32(addr):
    buf = ctypes.create_string_buffer(4)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 4, ctypes.byref(read)):
        return struct.unpack('<I', buf.raw)[0]
    return 0

def r64(addr):
    buf = ctypes.create_string_buffer(8)
    read = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, 8, ctypes.byref(read)):
        return struct.unpack('<Q', buf.raw)[0]
    return 0

def rstr(addr):
    size = r64(addr + 16)
    cap = r64(addr + 24)
    if size == 0 or size > 100: return ""
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    ptr = addr if cap < 16 else r64(addr)
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(ptr), buf, size, ctypes.byref(read)):
        return buf.raw[:read.value].decode('utf-8', errors='replace')
    return ""

fleet_mgr = r64(base + 0x3113008)
fleet_arr = r64(fleet_mgr + 0x18)

for fid in [37, 94]:
    p = r64(fleet_arr + (fid & 0xFFFFFF) * 16 + 8)
    print(f"\n--- Fleet {fid} (at {hex(p)}) ---")
    if p:
        # Check all string offsets in p
        for off in range(0, 0x190, 8):
            s = rstr(p + off)
            if s and not s.endswith('_object'):
                print(f"  String at +{hex(off)}: {repr(s)}")
        # Check if there is a template_id in CFleet
        # Where in CFleet is template_id?
        # For Fleet 3, template_id is 0!
        print(f"  Fleet 3 template check: for p3, what is template_id?")
