import ctypes
import win32process
import win32api
import subprocess

out = subprocess.check_output("tasklist /FI \"IMAGENAME eq stellaris.exe\" /FO CSV", shell=True).decode('gbk', errors='ignore')
lines = [l.strip().split('","') for l in out.strip().splitlines() if "stellaris.exe" in l]
pid = int(lines[0][1].strip('"'))
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_u64(addr):
    buf = ctypes.c_uint64()
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, 8, ctypes.byref(read))
    return buf.value

def read_u32(addr):
    buf = ctypes.c_uint32()
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, 4, ctypes.byref(read))
    return buf.value

colony_mgr = read_u64(base + 0x3110CF8)
cap = read_u32(colony_mgr + 0x20)
arr = read_u64(colony_mgr + 0x18)

queue_mgr = read_u64(base + 0x3112EB8)
q_cap = read_u32(queue_mgr + 0x20)
q_arr = read_u64(queue_mgr + 0x18)

print(f"Colony mgr cap: {cap}, Queue mgr cap: {q_cap}")
for i in range(min(cap, 20)):
    c = read_u64(arr + i * 16 + 8)
    if c:
        qid = read_u32(c + 0x1124)
        qid_slot = qid & 0xffffff
        q_obj = read_u64(q_arr + qid_slot * 16 + 8) if qid_slot < q_cap else 0
        planet_id = read_u32(q_obj + 0x98) if q_obj else None
        print(f"Colony {i}: qid={qid} (slot {qid_slot}), q_obj=0x{q_obj:X}, planet_id_in_queue={planet_id}")
