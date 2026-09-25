import sys, os, ctypes
sys.path.append(r"D:\stellarismcp\scripts")
import reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
PROCESS_ALL_ACCESS = 0x1F0FFF
h_proc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

def rp(a):
    v = ctypes.c_uint64()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 8, None)
    return v.value

def ru32(a):
    v = ctypes.c_uint32()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(a), ctypes.byref(v), 4, None)
    return v.value

vt = 0x7FF779F169D0
col = rp(vt - 8)
print(f"col: 0x{col:X}")
buf = (ctypes.c_uint32 * 10)()
kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(col), buf, 40, None)
for i in range(10):
    print(f"col + {i*4}: 0x{buf[i]:X}")

# In MSVC 64-bit:
# struct _RTTICompleteObjectLocator {
#     unsigned long signature; // 0 or 1
#     unsigned long offset;
#     unsigned long cdOffset;
#     unsigned long typeDescriptor; // RVA relative to image base
#     unsigned long classDescriptor; // RVA relative to image base
#     unsigned long objectBase; // RVA of image base
# };
td_rva = buf[3]
img_base_rva = buf[5]
print(f"td_rva: 0x{td_rva:X}, img_base_rva: 0x{img_base_rva:X}")
img_base = (col - img_base_rva) # wait, objectBase is RVA of locator itself?
# Actually: base = (vt - 8) is NOT img_base. 
# In MSVC x64, objectBase is the RVA of the CompleteObjectLocator from imageBase!
# So img_base = col - buf[5]!
img_base = col - buf[5]
print(f"Computed img_base: 0x{img_base:X}, actual base: 0x{base:X}")

td = img_base + td_rva
name_buf = (ctypes.c_char * 64)()
kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(td + 16), name_buf, 64, None)
print(f"TypeDescriptor name: {bytes(name_buf).split(b'\x00')[0].decode('latin1')}")
