import sys; sys.path.append('scripts')
import ctypes, inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
hProc = reload_dll.kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_u64(addr):
    buf = ctypes.c_uint64()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 8, None)
    return buf.value

def read_pdx_string(addr):
    buf = (ctypes.c_char * 32)()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 32, None)
    raw_size = int.from_bytes(buf[16:24], 'little')
    raw_cap = int.from_bytes(buf[24:32], 'little')
    if raw_size == 0 or raw_size > 500: return ''
    if raw_cap < 16: return bytes(buf[:raw_size]).decode('utf-8', errors='ignore')
    ptr = int.from_bytes(buf[0:8], 'little')
    if ptr:
        sbuf = (ctypes.c_char * raw_size)()
        reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(ptr), ctypes.byref(sbuf), raw_size, None)
        return bytes(sbuf).decode('utf-8', errors='ignore')
    return ''

comp_db = read_u64(base + 0x3156198)
arr = read_u64(comp_db + 0x20)
cnt = read_u64(comp_db + 0x28) & 0xFFFFFFFF

for i in range(cnt):
    set_ptr = read_u64(arr + i * 8)
    sk = read_pdx_string(set_ptr + 0x18)
    if 'PLASMA' in sk:
        c_vec = read_u64(set_ptr + 0xB8)
        c_cnt = read_u64(set_ptr + 0xC0) & 0xFFFFFFFF
        variants = []
        for j in range(c_cnt):
            tmpl = read_u64(c_vec + j * 8)
            tk = read_pdx_string(tmpl + 0x1B0)
            variants.append(tk)
        print(f"Set: '{sk}' -> {variants}")

reload_dll.kernel32.CloseHandle(hProc)
