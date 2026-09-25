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

def read_str(addr):
    buf = ctypes.create_string_buffer(128)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, 128, ctypes.byref(read))
    s = buf.raw.split(b'\0')[0]
    return s.decode('utf-8', errors='ignore')

# 0x14CCC58: lea rdx, [rip + 0xf88419] -> next rip = base + 0x14CCC5F
str1 = base + 0x14CCC5F + 0xf88419
print(f"str1 (0x14CCC58): '{read_str(str1)}'")

# 0x14CCC5F: lea rcx, [rip + 0xf883aa] -> next rip = base + 0x14CCC66
str2 = base + 0x14CCC66 + 0xf883aa
print(f"str2 (0x14CCC5F): '{read_str(str2)}'")

# 0x14CCD8E: lea rax, [rip + 0xf88303] -> next rip = base + 0x14CCD95
str3 = base + 0x14CCD95 + 0xf88303
print(f"str3 (0x14CCD8E): '{read_str(str3)}'")

# 0x14CCDD9: lea rax, [rip + 0xf88038] -> next rip = base + 0x14CCDE0
str4 = base + 0x14CCDE0 + 0xf88038
print(f"str4 (0x14CCDD9): '{read_str(str4)}'")

# 0x14CCE14: lea rax, [rip + 0xf882bd] -> next rip = base + 0x14CCE1B
str5 = base + 0x14CCE1B + 0xf882bd
print(f"str5 (0x14CCE14): '{read_str(str5)}'")

# 0x14CCEA2: lea rax, [rip + 0xecd31f] -> next rip = base + 0x14CCEA9
str6 = base + 0x14CCEA9 + 0xecd31f
print(f"str6 (0x14CCEA2): '{read_str(str6)}'")

# 0x14CCEE9: lea rbx, [rip + 0xf87f48] -> next rip = base + 0x14CCEF0
str7 = base + 0x14CCEF0 + 0xf87f48
print(f"str7 (0x14CCEE9): '{read_str(str7)}'")

# 0x14CCF00: lea rax, [rip + 0xf87ed1] -> next rip = base + 0x14CCF07
str8 = base + 0x14CCF07 + 0xf87ed1
print(f"str8 (0x14CCF00): '{read_str(str8)}'")

# 0x14CCF23: lea rax, [rip + 0xf87ece] -> next rip = base + 0x14CCF2A
str9 = base + 0x14CCF2A + 0xf87ece
print(f"str9 (0x14CCF23): '{read_str(str9)}'")
