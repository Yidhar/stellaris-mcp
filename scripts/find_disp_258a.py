import ctypes
import win32process
import win32api
import struct

pid = 86652
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

target_rva = 0x258A2F0

chunk_size = 1024 * 1024
for off in range(0x1000, 0x1800000, chunk_size):
    buf = ctypes.create_string_buffer(chunk_size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(base + off), buf, chunk_size, ctypes.byref(read))
    raw = buf.raw[:read.value]
    for insn_len in [5, 6, 7, 8]:
        for pos in range(len(raw) - 4):
            disp32 = struct.unpack('<i', raw[pos:pos+4])[0]
            cur_rva = off + pos
            dest = (cur_rva + (insn_len - (cur_rva - (off + pos)))) + disp32
            # simpler: if pos - (insn_len - 4) is instruction start, next rip is pos - (insn_len - 4) + insn_len = pos + 4
            next_rip = cur_rva + 4
            if next_rip + disp32 == target_rva:
                print(f"Found disp32 at RVA 0x{cur_rva:X} (insn start ~0x{cur_rva - (insn_len-4):X})")
