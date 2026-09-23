import ctypes, struct, sys
sys.stdout.reconfigure(encoding='utf-8')

kernel32 = ctypes.windll.kernel32
ReadProcessMemory = kernel32.ReadProcessMemory
pid = 104400
base = 0x7ff75ed50000
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

# Search in the EXE image or heap for leader_trait_
# In EXE image:
with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb") as f:
    exe_data = f.read()

import re
traits_in_exe = set(m.group(0).decode() for m in re.finditer(rb'leader_trait_[a-z0-9_]+', exe_data))
print(f"Found {len(traits_in_exe)} leader_trait_* in exe:")
for t in sorted(traits_in_exe)[:10]:
    print(" ", t)

# Search memory of PID 104400 for occurrences of 'leader_trait_principled' or 'leader_trait_charismatic'
# using VirtualQueryEx
MEMORY_BASIC_INFORMATION = ctypes.c_char * 48

class MBI(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p),
        ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", ctypes.c_uint32),
        ("PartitionId", ctypes.c_uint16),
        ("RegionSize", ctypes.c_size_t),
        ("State", ctypes.c_uint32),
        ("Protect", ctypes.c_uint32),
        ("Type", ctypes.c_uint32),
    ]

VirtualQueryEx = kernel32.VirtualQueryEx
VirtualQueryEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
VirtualQueryEx.restype = ctypes.c_size_t

addr = 0
ruler_ptr = 0x179df9eb378

# Search for heap addresses that contain leader_trait_ and see if ruler has pointers near them
trait_objects = []
while addr < 0x7FFFFFFFFFFF:
    mbi = MBI()
    if not VirtualQueryEx(hProc, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
        break
    if mbi.State == 0x1000 and (mbi.Protect & 0xEE): # MEM_COMMIT and readable
        size = mbi.RegionSize
        if size < 100 * 1024 * 1024:
            buf = ctypes.create_string_buffer(min(size, 1024*1024))
            read = ctypes.c_size_t()
            if ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, min(size, 1024*1024), ctypes.byref(read)):
                raw = buf.raw[:read.value]
                if b"leader_trait_" in raw:
                    # Find offsets
                    pos = 0
                    while True:
                        idx = raw.find(b"leader_trait_", pos)
                        if idx == -1: break
                        # extract string
                        s = raw[idx:idx+64].split(b'\x00')[0].decode('latin-1', errors='ignore')
                        trait_objects.append((addr + idx, s))
                        pos = idx + len(s)
                        if len(trait_objects) > 20: break
    addr += mbi.RegionSize
    if len(trait_objects) > 20: break

print(f"\nFound {len(trait_objects)} trait strings in memory:")
for taddr, s in trait_objects[:10]:
    print(f"  0x{taddr:X}: '{s}'")

