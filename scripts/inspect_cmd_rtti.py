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
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), ctypes.byref(buf), 8, ctypes.byref(read))
    return buf.value

vt = base + 0xACDC04 + 0x18f2df4
print(f"Command vtable: 0x{vt - base:X}")

# What is [vt - 8] (RTTI Complete Object Locator)?
col = read_u64(vt - 8)
print(f"COL: 0x{col - base:X}")
td_rva = read_u64(col + 12) # in x64 it's relative offset
# Let's read string from type descriptor
import struct
buf = ctypes.create_string_buffer(64)
read = ctypes.c_size_t()
kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(col), buf, 64, ctypes.byref(read))
# In MSVC x64 COL struct:
# dword signature, dword offset, dword cdOffset, dword pTypeDescriptor (RVA), dword pClassDescriptor (RVA)
sig, off, cdoff, td_rva, cd_rva = struct.unpack('<IIIII', buf.raw[:20])
print(f"td_rva: 0x{td_rva:X}")
td_addr = base + td_rva
# TypeDescriptor: void* pVFTable, void* spare, char name[]
kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(td_addr + 16), buf, 64, ctypes.byref(read))
print(f"Command class name: {buf.raw.split(b'\\0')[0].decode('utf-8', errors='ignore')}")

# What is action_obj vtable at 0xACDBE0?
# lea rcx, [rip + 0x1874849] at 0xACDBE0 -> next rip = base + 0xACDBE7
act_vt = base + 0xACDBE7 + 0x1874849
print(f"Action vtable: 0x{act_vt - base:X}")
col2 = read_u64(act_vt - 8)
kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(col2), buf, 64, ctypes.byref(read))
sig, off, cdoff, td_rva2, cd_rva = struct.unpack('<IIIII', buf.raw[:20])
kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(base + td_rva2 + 16), buf, 64, ctypes.byref(read))
print(f"Action class name: {buf.raw.split(b'\\0')[0].decode('utf-8', errors='ignore')}")
