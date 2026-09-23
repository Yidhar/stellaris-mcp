import ctypes
import inject, reload_dll

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, "stellaris.exe")
hProc = reload_dll.kernel32.OpenProcess(0x1F0FFF, False, pid)

fn_loc_addr = base + 0x16D2D0
fn_free_addr = base + 0x15BBE0

# Let's allocate memory in target process for:
# 1. key string
# 2. StringView (16 bytes)
# 3. PdxCString (48 bytes)
# 4. shellcode to call fn_loc and read back result

test_key = b"KINETIC_ARTILLERY_2"
MEM_COMMIT = 0x1000
PAGE_EXECUTE_READWRITE = 0x40

alloc_addr = reload_dll.kernel32.VirtualAllocEx(hProc, None, 4096, MEM_COMMIT, PAGE_EXECUTE_READWRITE)

# Layout:
# 0x000: test_key bytes + \0
# 0x040: StringView { data (u64), size (u64) }
# 0x080: PdxCString (48 bytes)
# 0x100: Shellcode

reload_dll.kernel32.WriteProcessMemory(hProc, ctypes.c_void_p(alloc_addr), test_key + b'\0', len(test_key) + 1, None)

sv = (ctypes.c_uint64 * 2)(alloc_addr, len(test_key))
reload_dll.kernel32.WriteProcessMemory(hProc, ctypes.c_void_p(alloc_addr + 0x40), ctypes.byref(sv), 16, None)

zero_buf = (ctypes.c_char * 48)()
reload_dll.kernel32.WriteProcessMemory(hProc, ctypes.c_void_p(alloc_addr + 0x80), ctypes.byref(zero_buf), 48, None)

# Shellcode:
# sub rsp, 40
# mov rcx, alloc_addr + 0x80 (out_cstr)
# mov rdx, alloc_addr + 0x40 (in_sv)
# mov rax, fn_loc_addr
# call rax
# add rsp, 40
# ret

import struct
shellcode = bytearray()
shellcode += b'\x48\x83\xEC\x28' # sub rsp, 0x28
shellcode += b'\x48\xB9' + struct.pack('<Q', alloc_addr + 0x80) # mov rcx, out_cstr
shellcode += b'\x48\xBA' + struct.pack('<Q', alloc_addr + 0x40) # mov rdx, in_sv
shellcode += b'\x48\xB8' + struct.pack('<Q', fn_loc_addr)       # mov rax, fn_loc_addr
shellcode += b'\xFF\xD0'                                         # call rax
shellcode += b'\x48\x83\xC4\x28'                                 # add rsp, 0x28
shellcode += b'\xC3'                                             # ret

reload_dll.kernel32.WriteProcessMemory(hProc, ctypes.c_void_p(alloc_addr + 0x100), (ctypes.c_char * len(shellcode))(*shellcode), len(shellcode), None)

hThread = reload_dll.kernel32.CreateRemoteThread(hProc, None, 0, ctypes.c_void_p(alloc_addr + 0x100), None, 0, None)
reload_dll.kernel32.WaitForSingleObject(hThread, 5000)
reload_dll.kernel32.CloseHandle(hThread)

# Read back PdxCString at alloc_addr + 0x80
res_buf = (ctypes.c_char * 48)()
reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(alloc_addr + 0x80), ctypes.byref(res_buf), 48, None)

meta = bytes(res_buf[:16])
buf_or_ptr = bytes(res_buf[16:32])
size = struct.unpack('<Q', res_buf[32:40])[0]
cap = struct.unpack('<Q', res_buf[40:48])[0]

print(f"Result PdxCString: size={size}, cap={cap}")
if cap < 16:
    text = buf_or_ptr[:size].decode('utf-8', errors='ignore')
    print(f"Local short text: '{text}'")
else:
    ptr = struct.unpack('<Q', buf_or_ptr[:8])[0]
    print(f"Heap ptr: 0x{ptr:X}")
    if ptr:
        tbuf = (ctypes.c_char * size)()
        reload_dll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(ptr), ctypes.byref(tbuf), size, None)
        text = bytes(tbuf).decode('utf-8', errors='ignore')
        print(f"Heap text: '{text}'")

# Call free function on alloc_addr + 0x80
free_code = bytearray()
free_code += b'\x48\x83\xEC\x28'
free_code += b'\x48\xB9' + struct.pack('<Q', alloc_addr + 0x80)
free_code += b'\x48\xB8' + struct.pack('<Q', fn_free_addr)
free_code += b'\xFF\xD0'
free_code += b'\x48\x83\xC4\x28'
free_code += b'\xC3'
reload_dll.kernel32.WriteProcessMemory(hProc, ctypes.c_void_p(alloc_addr + 0x200), (ctypes.c_char * len(free_code))(*free_code), len(free_code), None)
hThread2 = reload_dll.kernel32.CreateRemoteThread(hProc, None, 0, ctypes.c_void_p(alloc_addr + 0x200), None, 0, None)
reload_dll.kernel32.WaitForSingleObject(hThread2, 5000)
reload_dll.kernel32.CloseHandle(hThread2)

reload_dll.kernel32.VirtualFreeEx(hProc, ctypes.c_void_p(alloc_addr), 0, 0x8000)
reload_dll.kernel32.CloseHandle(hProc)
