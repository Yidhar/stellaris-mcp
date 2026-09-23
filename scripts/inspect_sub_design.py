import pefile, ctypes
from ctypes import wintypes
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
hProc = reload_dll.kernel32.OpenProcess(0x1F0FFF, False, pid)

# Let's inspect design 1606
# Player country: read from base + 0x31561B0 (or find in country designs)
def read_u64(addr):
    buf = ctypes.c_uint64()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 8, None)
    return buf.value

def read_u32(addr):
    buf = ctypes.c_uint32()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 4, None)
    return buf.value

def read_pdx_string(addr):
    buf = (ctypes.c_char * 32)()
    reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), ctypes.byref(buf), 32, None)
    raw_size = int.from_bytes(buf[16:24], 'little')
    raw_cap = int.from_bytes(buf[24:32], 'little')
    if raw_size == 0 or raw_size > 500:
        return ""
    if raw_cap < 16:
        return bytes(buf[:raw_size]).decode('utf-8', errors='ignore')
    else:
        ptr = int.from_bytes(buf[0:8], 'little')
        if ptr:
            sbuf = (ctypes.c_char * raw_size)()
            reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(ptr), ctypes.byref(sbuf), raw_size, None)
            return bytes(sbuf).decode('utf-8', errors='ignore')
    return ""

p_state = read_u64(base + 0x31561B0)
p_game = read_u64(p_state + 0x88)
p_c_mgr = read_u64(p_game + 0x240)
p_player_c = read_u64(p_c_mgr + 0x140)

# Country designs: +0x1AD0
arr_ptr = read_u64(p_player_c + 0x1AD0)
count = read_u32(p_player_c + 0x1ADC)
print(f"Designs count: {count}")

# Find design 1606
# ShipDesignManager: +0x248 in p_game
p_des_mgr = read_u64(p_game + 0x248)
p_des_map = read_u64(p_des_mgr + 0x10) # unordered_map or vector?

for i in range(count):
    did = read_u32(arr_ptr + i * 4)
    if did == 1606:
        print(f"Found design 1606 at index {i}")

reload_dll.kernel32.CloseHandle(hProc)
