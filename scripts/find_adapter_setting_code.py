import ctypes
import win32process
import win32api
import capstone

pid = 86652
h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

# Search .rdata for "Loading settings for adapter"
import struct
pattern = b"Loading settings for adapter"
chunk_size = 1024 * 1024
str_addr = None
for off in range(0x1800000, 0x3000000, chunk_size):
    buf = ctypes.create_string_buffer(chunk_size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(base + off), buf, chunk_size, ctypes.byref(read))
    raw = buf.raw[:read.value]
    pos = raw.find(pattern)
    if pos != -1:
        str_addr = base + off + pos
        print(f"Found string at 0x{str_addr - base:X}")
        break

if str_addr:
    # Now find references to this string in .text
    str_bytes = struct.pack('<Q', str_addr)
    # or lea rdx, [rip + ...]
    for off in range(0x1000, 0x1800000, chunk_size):
        buf = ctypes.create_string_buffer(chunk_size)
        read = ctypes.c_size_t()
        kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(base + off), buf, chunk_size, ctypes.byref(read))
        raw = buf.raw[:read.value]
        # scan for lea
        cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        cs.detail = True
        # search for 48 8d (lea)
        pos = 0
        while True:
            pos = raw.find(b'\x48\x8d', pos)
            if pos == -1 or pos + 7 > len(raw):
                break
            code = raw[pos:pos+7]
            for insn in cs.disasm(code, base + off + pos):
                for op in insn.operands:
                    if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                        target = insn.address + insn.size + op.mem.disp
                        if target == str_addr:
                            print(f"Found reference to string at RVA 0x{insn.address - base:X}")
            pos += 1
