import ctypes
import sys
sys.path.append(r'D:\stellarismcp\scripts')
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = ctypes.windll.kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def rp64(addr):
    v = ctypes.c_uint64()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(v), 8, None)
    return v.value

def rp32(addr):
    v = ctypes.c_uint32()
    ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(v), 4, None)
    return v.value

def read_pdx_string(addr):
    size = rp64(addr + 0x10)
    if size < 16:
        buf = (ctypes.c_char * 16)()
        ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, 16, None)
        return bytes(buf).split(b'\x00')[0].decode('utf-8', errors='ignore')
    else:
        ptr = rp64(addr)
        if not ptr: return ''
        buf = (ctypes.c_char * min(size + 1, 128))()
        ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(ptr), buf, len(buf), None)
        return bytes(buf).split(b'\x00')[0].decode('utf-8', errors='ignore')

def resolve_planet(p_id):
    # 1. Direct Planet DB (0x3113128)
    db_p = rp64(base + 0x3113128)
    arr_p = rp64(db_p + 0x18)
    cap_p = rp32(db_p + 0x20)
    
    if (p_id & 0xFFFFFF) < cap_p:
        p_obj = rp64(arr_p + (p_id & 0xFFFFFF) * 16 + 8)
        if p_obj:
            has_colony = rp32(p_obj + 8)
            if has_colony != 0:
                name = read_pdx_string(p_obj + 0x108)
                qid = rp32(p_obj + 0xe4)
                return p_obj, (p_id & 0xFFFFFF), name, qid

    # 2. System DB (0x3113148)
    db_s = rp64(base + 0x3113148)
    arr_s = rp64(db_s + 0x18)
    cap_s = rp32(db_s + 0x20)
    if (p_id & 0xFFFFFF) < cap_s:
        s_obj = rp64(arr_s + (p_id & 0xFFFFFF) * 16 + 8)
        if s_obj:
            p_vec = rp64(s_obj + 0x498)
            p_cnt = rp32(s_obj + 0x4A4)
            if p_vec and p_cnt > 0 and p_cnt < 100:
                for i in range(p_cnt):
                    sub_pid = rp32(p_vec + i * 4)
                    if (sub_pid & 0xFFFFFF) < cap_p:
                        cand = rp64(arr_p + (sub_pid & 0xFFFFFF) * 16 + 8)
                        if cand and rp32(cand + 8) != 0:
                            name = read_pdx_string(cand + 0x108)
                            qid = rp32(cand + 0xe4)
                            return cand, sub_pid, name, qid

    return 0, 0, 'NOT_FOUND', -1

for test_id in [11, 3, 63, 752]:
    ptr, real_pid, name, qid = resolve_planet(test_id)
    print(f'Test ID {test_id:3d} -> ptr={hex(ptr)}, real_planet_id={real_pid}, name="{name}", queue_id={qid}')
