import win32process, win32api, struct

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

p_popmgr = r64(base + 0x3113128)
p_poparr = r64(p_popmgr + 0x18)

for k in range(24):
    pop = r64(p_poparr + k * 16 + 8)
    name = rpdx(pop + 0xF0)
    # Check if pop has a species pointer or id
    sp_ptr = r64(pop + 0x20 + 0xB50)
    print(f'Pop {k}: name="{name}", sp_ptr=0x{sp_ptr:X}')
