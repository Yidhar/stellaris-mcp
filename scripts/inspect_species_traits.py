import win32process, win32api, struct, sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

hProc = win32api.OpenProcess(0x1F0FFF, False, 87664)
base = 0x7FF777BD0000

def r64(a):
    try: return struct.unpack('<Q', win32process.ReadProcessMemory(hProc, a, 8))[0]
    except: return 0

def r32(a):
    try: return struct.unpack('<I', win32process.ReadProcessMemory(hProc, a, 4))[0]
    except: return 0

def rpdx(a):
    try:
        sz = r64(a + 16)
        cap = r64(a + 24)
        if sz == 0 or sz > 200: return ''
        if cap < 16:
            raw = win32process.ReadProcessMemory(hProc, a, 16)
            return raw[:sz].decode('latin-1', errors='ignore')
        else:
            ptr = r64(a)
            raw = win32process.ReadProcessMemory(hProc, ptr, sz)
            return raw[:sz].decode('latin-1', errors='ignore')
    except: return ''

p_smgr = r64(base + 0x3112F58)
p_sp_arr = r64(p_smgr + 0x18)
pHuman = r64(p_sp_arr + 1 * 16 + 8)

print(f"Scanning pHuman (0x{pHuman:X}) from 0x180 to 0x540:")
for off in range(0x180, 0x540, 8):
    q = r64(pHuman + off)
    s = rpdx(pHuman + off)
    note = f'str="{s}"' if s else ''
    if q > 0x10000000000:
        sub_s = rpdx(q)
        if sub_s: note += f' ptr_str="{sub_s}"'
        vt = r64(q) - base
        if 0 < vt < 0x3000000: note += f' vt_rva=0x{vt:X}'
    if note or q > 0x10000000000:
        print(f"+0x{off:03X}: 0x{q:016X}  {note}")
