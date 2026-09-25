import ctypes
import win32process
import win32api

pid = 86652
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

target_addr = base + 0x258A2F0

# Scan .text (0x1000 to 0x1800000) for 4-byte RIP displacement pointing to target_addr
# At instruction: lea reg, [rip + disp32] (48 8d ...)
# instruction length is 7 bytes (e.g. 48 8d 15 disp32)
# disp32 = target_addr - (cur_addr + 7)
import struct
chunk_size = 1024 * 1024
for off in range(0x1000, 0x1800000, chunk_size):
    buf = ctypes.create_string_buffer(chunk_size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(base + off), buf, chunk_size, ctypes.byref(read))
    raw = buf.raw[:read.value]
    for pos in range(len(raw) - 7):
        if raw[pos] == 0x48 and raw[pos+1] == 0x8d: # lea reg, [rip + disp]
            disp32 = struct.unpack('<i', raw[pos+3:pos+7])[0]
            cur_rva = off + pos
            dest = cur_rva + 7 + disp32
            if dest == 0x258A2F0:
                print(f"Found lea at RVA 0x{cur_rva:X}")
