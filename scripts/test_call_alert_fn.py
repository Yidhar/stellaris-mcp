import sys, struct
sys.path.append(r'D:\stellarismcp\scripts')
import ctypes, reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
h_proc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def rp(a):
    v = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 8, None)
    return v.value

cmgr = rp(base + 0x3113140)
arr = rp(cmgr + 0x18)
s0 = rp(arr + 8) # Earth
print(f'Earth s0: 0x{s0:X}')

fn_14d8d00 = base + 0x14d8d00

# Allocate memory in remote process: 0x1000 bytes
remote_mem = kernel32.VirtualAllocEx(h_proc, None, 0x1000, 0x3000, 0x40) # PAGE_EXECUTE_READWRITE
print(f'remote_mem: 0x{remote_mem:X}')

dummy_ctx_addr = remote_mem
results_addr = remote_mem + 0x100
code_addr = remote_mem + 0x120

# Zero out dummy_ctx and results
zero_buf = (ctypes.c_char * 0x120)()
kernel32.WriteProcessMemory(h_proc, ctypes.c_void_p(remote_mem), zero_buf, 0x120, None)

sc = bytearray()
sc += b'\x48\x83\xEC\x38' # sub rsp, 0x38
sc += b'\x56'             # push rsi
sc += b'\x53'             # push rbx
sc += b'\x31\xF6'         # xor esi, esi

loop_offset = len(sc)
sc += b'\x48\xB9' + struct.pack('<Q', dummy_ctx_addr) # mov rcx, dummy_ctx_addr
sc += b'\x89\xF2'                                     # mov edx, esi
sc += b'\x49\xB8' + struct.pack('<Q', s0)             # mov r8, s0
sc += b'\x45\x31\xC9'                                 # xor r9d, r9d
sc += b'\x48\xC7\x44\x24\x20\x00\x00\x00\x00'         # mov qword ptr [rsp + 0x20], 0
sc += b'\x48\xB8' + struct.pack('<Q', fn_14d8d00)     # mov rax, fn_14d8d00
sc += b'\xFF\xD0'                                     # call rax
sc += b'\x48\xBB' + struct.pack('<Q', results_addr)   # mov rbx, results_addr
sc += b'\x88\x04\x33'                                 # mov byte ptr [rbx + rsi], al
sc += b'\xFF\xC6'                                     # inc esi
sc += b'\x83\xFE\x0F'                                 # cmp esi, 15

# jl loop_offset
rel = loop_offset - (len(sc) + 2)
sc += b'\x7C' + struct.pack('<b', rel)

sc += b'\x5B'             # pop rbx
sc += b'\x5E'             # pop rsi
sc += b'\x48\x83\xC4\x38' # add rsp, 0x38
sc += b'\x31\xC0'         # xor eax, eax
sc += b'\xC3'             # ret

kernel32.WriteProcessMemory(h_proc, ctypes.c_void_p(code_addr), (ctypes.c_char * len(sc)).from_buffer(sc), len(sc), None)

h_thread = kernel32.CreateRemoteThread(h_proc, None, 0, ctypes.c_void_p(code_addr), None, 0, None)
res = kernel32.WaitForSingleObject(h_thread, 3000)
print(f'Thread wait result: {res}')

# Read results
res_buf = (ctypes.c_char * 16)()
kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(results_addr), res_buf, 16, None)

alert_names = {
    0: "0: OUTLINER_PLANET_CONSTRUCTION_AVAILABLE (建筑槽位可用)",
    1: "1: OUTLINER_PLANET_BLOCKER_AVAILABLE (障碍物可清除)",
    2: "2: OUTLINER_PLANET_UPGRADE_AVAILABLE (首府建筑可升级)",
    3: "3: OUTLINER_STARBASE_CONSTRUCTION_AVAILABLE",
    4: "4: OUTLINER_STARBASE_UPGRADE_AVAILABLE",
    5: "5: OUTLINER_PLANET_UNEMPLOYMENT_PRESENT (失业人口)",
    6: "6: OUTLINER_PLANET_AUTOM_MIGRATION_PRESENT (居民过饱和)",
    7: "7: OUTLINER_PLANET_OVERCROWDING_PRESENT (人口拥挤/住房不足)",
    8: "8: unused",
    9: "9: unused",
    10: "10: OUTLINER_PLANET_LOW_STABILITY (低稳定度)",
    11: "11: starbase",
    12: "12: starbase",
    13: "13: starbase",
    14: "14: starbase",
}

print("Live Alert Check on Earth (0x14d8d00):")
for i in range(15):
    val = res_buf[i][0]
    name = alert_names.get(i, str(i))
    if val != 0:
        print(f"  >>> ACTIVE: {name} (val={val})")
    else:
        print(f"      inactive: {name}")

kernel32.VirtualFreeEx(h_proc, ctypes.c_void_p(remote_mem), 0, 0x8000)
